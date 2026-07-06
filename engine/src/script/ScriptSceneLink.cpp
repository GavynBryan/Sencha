#include <script/ScriptSceneLink.h>

#include <app/GameContexts.h>
#include <core/assets/AssetSystem.h>
#include <core/json/JsonValue.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <script/ScriptBehaviorSystem.h>
#include <script/ScriptComponents.h>
#include <script/ScriptLink.h>
#include <script/ScriptRuntime.h>

#include <set>
#include <string>
#include <string_view>

namespace
{
    // Reads a ScriptSource-style asset field value: either a bare path string
    // or an AssetRef object carrying a "path". Empty if neither.
    std::string ReadScriptPath(const JsonValue& field)
    {
        if (field.IsString())
        {
            return field.AsString();
        }
        if (field.IsObject())
        {
            if (const JsonValue* path = field.Find("path"); path != nullptr && path->IsString())
            {
                return path->AsString();
            }
        }
        return {};
    }

    // Collects unique ScriptSource script paths from a parsed scene.
    void CollectScriptPaths(const JsonValue& sceneJson, std::vector<std::string>& out)
    {
        const JsonValue* entities = sceneJson.IsObject() ? sceneJson.Find("entities") : nullptr;
        if (entities == nullptr || !entities->IsArray())
        {
            return;
        }
        const std::string_view key = TypeSchema<ScriptSource>::Name;
        std::set<std::string> seen;
        for (const JsonValue& entity : entities->AsArray())
        {
            if (!entity.IsObject())
            {
                continue;
            }
            const JsonValue* components = entity.Find("components");
            if (components == nullptr || !components->IsObject())
            {
                continue;
            }
            const JsonValue* source = components->Find(key);
            if (source == nullptr || !source->IsObject())
            {
                continue;
            }
            const JsonValue* script = source->Find("script");
            if (script == nullptr)
            {
                continue;
            }
            const std::string path = ReadScriptPath(*script);
            if (!path.empty() && seen.insert(path).second)
            {
                out.push_back(path);
            }
        }
    }

    // The first behavior declaration in a module, or -1.
    std::int32_t FirstBehaviorDeclaration(const ScriptModule& module)
    {
        for (std::size_t i = 0; i < module.Declarations.size(); ++i)
        {
            if (module.Declarations[i].Kind == ScriptDeclKind::Behavior)
            {
                return static_cast<std::int32_t>(i);
            }
        }
        return -1;
    }
}

void LinkScriptsForScene(World& world, AssetSystem& assets, const JsonValue& sceneJson,
                         std::uint64_t worldSeed)
{
    // The runtime + its authored/runtime components must exist before the
    // finalize pass creates entities; this runs in the build phase.
    RegisterScriptRuntime(world, worldSeed);

    std::vector<std::string> paths;
    CollectScriptPaths(sceneJson, paths);
    if (paths.empty())
    {
        return;
    }

    ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();
    for (const std::string& path : paths)
    {
        const ScriptHandle handle = assets.LoadScript(path);
        if (!handle.IsValid())
        {
            continue; // load failure already logged by the loader
        }
        // Dedup: a module already linked in this world keeps its slot.
        bool linked = false;
        for (const ScriptHandle existing : runtime.ModuleHandles)
        {
            if (existing == handle)
            {
                linked = true;
                break;
            }
        }
        if (linked)
        {
            continue;
        }
        std::shared_ptr<const ScriptModule> module = assets.GetScriptModule(handle);
        if (module == nullptr)
        {
            continue;
        }
        const ScriptLinkResult result = LinkScriptModule(world, module);
        if (result.Ok)
        {
            runtime.ModuleHandles[result.ModuleIndex] = handle;
        }
    }
}

void ScriptResolveSystem::Step(World& world)
{
    if (!world.HasResource<ScriptRuntime>() || !world.IsRegistered<ScriptSource>()
        || !world.IsRegistered<ScriptBehavior>())
    {
        return;
    }
    const ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();

    CommandBuffer commands(world);
    world.ForEachComponent<ScriptSource>([&](EntityId entity, ScriptSource& source) {
        if (world.HasComponent<ScriptBehavior>(entity))
        {
            return; // already resolved
        }
        // Map the authored handle to a linked module.
        std::int32_t moduleIndex = -1;
        for (std::size_t i = 0; i < runtime.ModuleHandles.size(); ++i)
        {
            if (runtime.ModuleHandles[i] == source.Script)
            {
                moduleIndex = static_cast<std::int32_t>(i);
                break;
            }
        }
        if (moduleIndex < 0)
        {
            return; // script not linked (missing manifest / build-phase link)
        }
        const ScriptModule& module = *runtime.Modules[moduleIndex].Module;
        const std::int32_t decl = FirstBehaviorDeclaration(module);
        if (decl < 0)
        {
            return; // the script defines no behavior to attach
        }
        ScriptBehavior behavior;
        behavior.LinkedModule = static_cast<std::uint32_t>(moduleIndex);
        behavior.Declaration = static_cast<std::uint32_t>(decl);
        commands.AddComponent(entity, behavior);
        if (world.IsRegistered<ScriptCueBuffer>() && !world.HasComponent<ScriptCueBuffer>(entity))
        {
            commands.AddComponent(entity, ScriptCueBuffer{});
        }
    });
    commands.Flush();
}

void ScriptResolveSystem::FixedLogic(FixedLogicContext& ctx)
{
    for (Registry* registry : ctx.ActiveRegistries)
    {
        if (registry != nullptr)
        {
            Step(registry->Components);
        }
    }
}
