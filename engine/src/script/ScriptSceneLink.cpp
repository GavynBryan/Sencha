#include <script/ScriptSceneLink.h>

#include <core/assets/AssetSystem.h>
#include <core/json/JsonValue.h>
#include <ecs/World.h>
#include <script/ScriptLink.h>
#include <script/ScriptRuntime.h>

#include <memory>
#include <string>

namespace
{
    // A `script_modules` entry is either a bare path string or an asset-ref
    // object carrying a "path". Empty if neither.
    std::string ReadScriptPath(const JsonValue& entry)
    {
        if (entry.IsString())
        {
            return entry.AsString();
        }
        if (entry.IsObject())
        {
            if (const JsonValue* path = entry.Find("path"); path != nullptr && path->IsString())
            {
                return path->AsString();
            }
        }
        return {};
    }
}

void LinkScriptsForScene(World& world, AssetSystem& assets, const JsonValue& sceneJson,
                         std::uint64_t worldSeed)
{
    // The runtime + its module script components must exist before the finalize
    // pass creates entities; this runs in the build phase.
    RegisterScriptRuntime(world, worldSeed);

    const JsonValue* modules =
        sceneJson.IsObject() ? sceneJson.Find("script_modules") : nullptr;
    if (modules == nullptr || !modules->IsArray())
    {
        return;
    }

    ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();
    for (const JsonValue& entry : modules->AsArray())
    {
        const std::string path = ReadScriptPath(entry);
        if (path.empty())
        {
            continue;
        }
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
