#pragma once

// A RuntimeWorld with a scene asset stack and a SceneSpawnService, zero-thread,
// for tests of what a spawn builds. SpawnMarker is a test component a scene can
// carry so a test can tell its entities apart.

#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <tuple>

struct SpawnMarker
{
    int Value = 0;
};

template <>
struct TypeSchema<SpawnMarker>
{
    static constexpr std::string_view Name = "spawn_marker";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'P', 'M', 'K');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SpawnMarker::Value),
        };
    }
};

struct SpawnHarness
{
    SpawnHarness()
        : Tasks(0)
        , Schema([] {
            WorldComponentSchema schema;
            schema.Add<LocalTransform>();
            schema.Add<WorldTransform>();
            schema.Add<Parent>();
            schema.Add<PersistentIdComponent>();
            schema.Add<SceneInstance>();
            schema.Add<SpawnMarker>();
            schema.Seal();
            return schema;
        }())
        , World(Schema)
        , Registry(Logging)
        , Scenes(Logging)
        , SceneLoader(Logging, &Scenes, &Serializers)
        , Assets(Logging, Registry)
        , Service(World, Schema, Serializers, Tasks, Logging)
    {
        // The engine's own serializer set covers transform, identity,
        // and scene_instance; the marker is this test's addition.
        RegisterEngineSceneSerializers(Serializers);
        EXPECT_EQ(Serializers.Register(
                      std::make_unique<ComponentSerializer<SpawnMarker>>()),
                  ComponentSerializerRegistry::RegisterResult::Added);
        RegisterAssetKind(Assets, AssetType::Scene, SceneLoader, &Scenes);
        Service.ConnectAssets(&Assets, &Scenes);
    }

    // One full turn of the async drain: worker runs, completion commits,
    // pump publishes.
    void Turn()
    {
        (void)Tasks.PumpWork();
        (void)Tasks.DrainCompletions();
        Service.Pump();
    }

    LoggingProvider Logging;
    AsyncTaskQueue Tasks;
    ComponentSerializerRegistry Serializers;
    WorldComponentSchema Schema;
    RuntimeWorld World;
    AssetRegistry Registry;
    SceneCache Scenes;
    SceneAssetLoader SceneLoader;
    AssetSystem Assets;
    SceneSpawnService Service;
};
