#include "ScriptTestSupport.h"

#include <assets/script/ScriptCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <script/ScriptBehaviorSystem.h>
#include <script/ScriptCompiler.h>
#include <script/ScriptComponents.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptSceneLink.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

//=============================================================================
// The designer-facing end to end, headless: an entity in a scene carries a
// ScriptSource pointing at a cooked T script; loading the scene through the
// real serializer links the module, the resolve system attaches the runtime
// behavior, and ticking runs the script against the entity's Transform. The
// scene is round-tripped through SaveSceneJson/LoadSceneJson, so this also
// covers the ScriptSource save/load path.
//=============================================================================

namespace
{
    // A self-contained level behavior: needs only the authored Transform, no
    // script-defined component to pre-place. It bobs the entity around y=100.
    constexpr std::string_view kLevelBob =
        "behavior LevelBob {\n"
        "    fn fixed(ctx: BehaviorContext) {\n"
        "        let t: f32 = f32(ctx.tick) * ctx.dt\n"
        "        ctx.entity.Transform.position.y = 100.0 + f32(sin(t * 2.0) * 10.0)\n"
        "    }\n"
        "}\n";

    float ReferenceBob(std::uint64_t tick, float dt)
    {
        const float t = static_cast<float>(static_cast<double>(tick) * static_cast<double>(dt));
        const float offset = static_cast<float>(std::sin(static_cast<double>(t) * 2.0) * 10.0);
        return 100.0f + offset;
    }

    struct TempFile
    {
        std::filesystem::path Path;
        explicit TempFile(std::string_view name)
        {
            std::random_device rd;
            Path = std::filesystem::temp_directory_path()
                 / ("sencha_script_scene_" + std::to_string(rd()) + "_" + std::string(name));
        }
        ~TempFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };
}

TEST(ScriptSceneE2E, LevelEntityWithScriptRunsAfterSceneLoad)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ScriptCache scriptCache(logging);
    AssetSystem assets(logging, registry, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, &scriptCache);

    // Cook the script to a .tbc on disk and register it as a Script asset.
    ScriptCompileResult compiled = CompileScript("levelbob.t", kLevelBob, {});
    ASSERT_TRUE(compiled.Ok) << compiled.Error.Message;
    const std::vector<std::byte> tbc =
        WriteScriptModule(compiled.Module);
    TempFile cooked("levelbob.tbc");
    {
        std::ofstream out(cooked.Path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(tbc.data()),
                  static_cast<std::streamsize>(tbc.size()));
    }
    AssetRecord record;
    record.Type = AssetType::Script;
    record.SourceKind = AssetSourceKind::File;
    record.Path = "asset://levelbob.t";
    record.FilePath = cooked.Path.generic_string();
    ASSERT_TRUE(registry.Register(record));

    // Serializers: built-in scene components (Transform) plus ScriptSource.
    InitSceneSerializer();
    RegisterComponent<ScriptSource>();

    SceneSerializationContext context(logging, &assets);

    // Author a scene: one entity at y=100 carrying a Transform and a
    // ScriptSource pointing at the script. Built by hand in a source registry,
    // then saved to JSON (the ScriptSource save path).
    JsonValue sceneJson;
    {
        Registry source;
        source.Components.RegisterComponent<LocalTransform>();
        source.Components.RegisterComponent<ScriptSource>();
        const EntityId entity = source.Components.CreateEntity();
        LocalTransform xform{};
        xform.Value.Position.X = 0.0f;
        xform.Value.Position.Y = 100.0f;
        xform.Value.Position.Z = 0.0f;
        source.Components.AddComponent(entity, xform);
        ScriptSource scriptComponent{};
        scriptComponent.Script = assets.LoadScript(record.Path);
        ASSERT_TRUE(scriptComponent.Script.IsValid());
        source.Components.AddComponent(entity, scriptComponent);

        sceneJson = SaveSceneJson(source, context);
    }
    // The saved scene carries the script asset path (not a runtime handle).
    ASSERT_TRUE(sceneJson.IsObject());

    // Load into a fresh world exactly as the zone loader would. The build
    // phase registers host component storage first (the game does this via
    // its component-registration helpers), so LinkScriptModule can resolve
    // the script's Transform field binds; then it links the scripts, both
    // before the finalize pass creates entities.
    Registry world;
    world.Components.RegisterComponent<LocalTransform>();
    LinkScriptsForScene(world.Components, assets, sceneJson, /*worldSeed*/ 7);
    {
        // Diagnostic: linking must have found and linked the script.
        const ScriptRuntime& rt = world.Components.GetResource<ScriptRuntime>();
        ASSERT_EQ(rt.Modules.size(), 1u) << "scene JSON:\n" << JsonStringify(sceneJson, true);
        ASSERT_TRUE(rt.ModuleHandles[0].IsValid());
    }
    SceneLoadError loadError;
    ASSERT_TRUE(LoadSceneJson(sceneJson, world, context, &loadError)) << loadError.Message;

    // The entity exists with its Transform and ScriptSource; no behavior yet.
    EntityId entity;
    world.Components.ForEachComponent<ScriptSource>([&](EntityId e, ScriptSource&) {
        entity = e;
    });
    ASSERT_TRUE(entity.IsValid());
    EXPECT_FALSE(world.Components.HasComponent<ScriptBehavior>(entity));

    // Resolve attaches the runtime behavior.
    ScriptResolveSystem resolve;
    resolve.Step(world.Components);
    ASSERT_TRUE(world.Components.HasComponent<ScriptBehavior>(entity));

    // Ticking runs the script: the entity bobs around y=100.
    ScriptBehaviorSystem behavior;
    const float dt = 1.0f / 60.0f;
    for (std::uint64_t tick = 1; tick <= 10; ++tick)
    {
        behavior.Step(world.Components, tick, dt);
        const LocalTransform* xf = world.Components.TryGet<LocalTransform>(entity);
        ASSERT_NE(xf, nullptr);
        EXPECT_NEAR(xf->Value.Position.Y, ReferenceBob(tick, dt), 0.02f) << "tick " << tick;
    }

    // A representative tick moved off the rest height (the script really ran).
    behavior.Step(world.Components, 12, dt);
    EXPECT_NE(world.Components.TryGet<LocalTransform>(entity)->Value.Position.Y, 100.0f);
}
