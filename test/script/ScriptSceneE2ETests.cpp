#include "ScriptTestSupport.h"

#include <assets/script/ScriptCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <script/ScriptCompiler.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptSceneLink.h>
#include <script/ScriptSystemTick.h>
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
// The designer-facing end to end, headless: a scene's `script_modules` manifest
// names a cooked T script; loading the scene through the real serializer links
// the module before entities are created, and ticking runs its `system` over
// the entity's authored Transform. The scene round-trips through
// SaveSceneJson/LoadSceneJson. LevelBob touches only the native Transform, so
// it leaves no authored script component: the manifest is what links it (the
// case a component scan cannot cover).
//=============================================================================

namespace
{
    constexpr std::string_view kLevelBob =
        "system LevelBob {\n"
        "    over Transform\n"
        "    writes Transform\n"
        "    fn fixed(ctx: FixedContext) {\n"
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
    const std::vector<std::byte> tbc = WriteScriptModule(compiled.Module);
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

    InitSceneSerializer();
    SceneSerializationContext context(logging, &assets);

    // Author a scene: one entity at y=100 carrying only a Transform (LevelBob
    // touches no script component). The scene's `script_modules` manifest names
    // the module to link.
    JsonValue sceneJson;
    {
        Registry source;
        source.Components.RegisterComponent<LocalTransform>();
        const EntityId entity = source.Components.CreateEntity();
        LocalTransform xform{};
        xform.Value.Position.X = 0.0f;
        xform.Value.Position.Y = 100.0f;
        xform.Value.Position.Z = 0.0f;
        source.Components.AddComponent(entity, xform);

        sceneJson = SaveSceneJson(source, context);
        ASSERT_TRUE(sceneJson.IsObject());
        JsonValue::Array modules;
        modules.push_back(JsonValue(std::string(record.Path)));
        sceneJson.AsObject().push_back({std::string("script_modules"), JsonValue(std::move(modules))});
    }

    // Load into a fresh world exactly as the zone loader would. The build phase
    // registers host component storage first, then links the manifest's modules
    // (so LinkScriptModule can resolve the script's Transform field binds), both
    // before the finalize pass creates entities.
    Registry world;
    world.Components.RegisterComponent<LocalTransform>();
    LinkScriptsForScene(world.Components, assets, sceneJson, /*worldSeed*/ 7);
    {
        const ScriptRuntime& rt = world.Components.GetResource<ScriptRuntime>();
        ASSERT_EQ(rt.Modules.size(), 1u) << "scene JSON:\n" << JsonStringify(sceneJson, true);
        ASSERT_TRUE(rt.ModuleHandles[0].IsValid());
    }
    SceneLoadError loadError;
    ASSERT_TRUE(LoadSceneJson(sceneJson, world, context, &loadError)) << loadError.Message;

    EntityId entity;
    world.Components.ForEachComponent<LocalTransform>([&](EntityId e, LocalTransform&) {
        entity = e;
    });
    ASSERT_TRUE(entity.IsValid());

    // Ticking runs the `system`: the entity bobs around y=100.
    ScriptSystemTick system;
    const float dt = 1.0f / 60.0f;
    for (std::uint64_t tick = 1; tick <= 10; ++tick)
    {
        system.Step(world.Components, tick, dt);
        const LocalTransform* xf = world.Components.TryGet<LocalTransform>(entity);
        ASSERT_NE(xf, nullptr);
        EXPECT_NEAR(xf->Value.Position.Y, ReferenceBob(tick, dt), 0.02f) << "tick " << tick;
    }

    // A representative tick moved off the rest height (the script really ran).
    system.Step(world.Components, 12, dt);
    EXPECT_NE(world.Components.TryGet<LocalTransform>(entity)->Value.Position.Y, 100.0f);
}
