#pragma once

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

struct MaterializedMarker
{
    int Value = 0;
};

template <>
struct TypeSchema<MaterializedMarker>
{
    static constexpr std::string_view Name = "materialized_marker";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('M', 'T', 'M', 'K');
    static constexpr bool AuthorityMaterialized = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &MaterializedMarker::Value),
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
            schema.Add<MaterializedMarker>();
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
        RegisterEngineSceneSerializers(Serializers);
        EXPECT_EQ(Serializers.Register(
                      std::make_unique<ComponentSerializer<SpawnMarker>>()),
                  ComponentSerializerRegistry::RegisterResult::Added);
        EXPECT_EQ(Serializers.Register(
                      std::make_unique<ComponentSerializer<MaterializedMarker>>()),
                  ComponentSerializerRegistry::RegisterResult::Added);
        RegisterAssetKind(Assets, AssetType::Scene, SceneLoader, &Scenes);
        Service.ConnectAssets(&Assets, &Scenes);
    }

    // Worker runs, completion commits, pump publishes.
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
