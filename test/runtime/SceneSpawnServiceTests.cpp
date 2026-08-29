// SceneSpawnService: request -> task-lane stage/build -> owner-thread pump,
// publishing in request order, group-addressable through SceneInstanceIndex,
// transient (authored persistent identity stripped). Zero-thread, so every
// stage is deterministic.

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include "SmapSceneFixture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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

namespace
{
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
            , Assets(Logging, Registry, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, &Scenes, &Serializers)
            , Service(World, Schema, Serializers, Tasks, Logging)
        {
            // The engine's own serializer set covers transform, identity,
            // and scene_instance; the marker is this test's addition.
            RegisterEngineSceneSerializers(Serializers);
            EXPECT_EQ(Serializers.Register(
                          std::make_unique<ComponentSerializer<SpawnMarker>>()),
                      ComponentSerializerRegistry::RegisterResult::Added);
            Service.ConnectAssets(&Assets);
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
        AssetSystem Assets;
        SceneSpawnService Service;
    };

    // A two-entity scene: a root at (1,2,3) with an authored persistent id,
    // and a child parented to it.
    [[nodiscard]] SmapContents MakeSpawnContents()
    {
        SmapContents contents;

        SmapEntityRecord root;
        root.Persistent = PersistentEntityId{ 0x77 };
        root.Components.emplace_back(
            MakeComponentTypeId("spawn_marker"),
            JsonValue(JsonValue::Object{ { "value", JsonValue(1.0) } }));
        root.Components.emplace_back(
            MakeComponentTypeId("persistent_id"),
            JsonValue(JsonValue::Object{
                { "id", JsonValue(std::string("0000000000000077")) } }));
        root.Components.emplace_back(
            MakeComponentTypeId("Transform"),
            JsonValue(JsonValue::Object{
                { "local", JsonValue(JsonValue::Object{
                    { "position", JsonValue(JsonValue::Array{
                        JsonValue(1.0), JsonValue(2.0), JsonValue(3.0) }) },
                    { "rotation", JsonValue(JsonValue::Array{
                        JsonValue(0.0), JsonValue(0.0), JsonValue(0.0),
                        JsonValue(1.0) }) },
                    { "scale", JsonValue(JsonValue::Array{
                        JsonValue(1.0), JsonValue(1.0), JsonValue(1.0) }) },
                }) } }));
        contents.Entities.push_back(std::move(root));

        SmapEntityRecord child;
        child.Parent = 0;
        child.Components.emplace_back(
            MakeComponentTypeId("spawn_marker"),
            JsonValue(JsonValue::Object{ { "value", JsonValue(2.0) } }));
        contents.Entities.push_back(std::move(child));
        return contents;
    }
} // namespace

TEST(SceneSpawnService, SpawnPublishesTransientEntitiesWithGroupIdentity)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "lamp");

    Transform3f root = Transform3f::Identity();
    root.Position = Vec3d(10.0f, 0.0f, 0.0f);
    const SceneSpawnId id = h.Service.RequestSpawn(scene.Path, root);
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Pending);
    EXPECT_TRUE(h.Service.Entities(id).empty());

    h.Turn();

    ASSERT_EQ(h.Service.Status(id), SceneSpawnStatus::Live);
    const std::span<const EntityId> members = h.Service.Entities(id);
    ASSERT_EQ(members.size(), 2u);

    World& world = h.World.Entities();
    SceneInstanceId group{};
    for (EntityId entity : members)
    {
        const SceneInstance* instance = world.TryGet<SceneInstance>(entity);
        ASSERT_NE(instance, nullptr);
        EXPECT_NE(instance->Id.Value & SceneInstanceIdRuntimeBit, 0u);
        group = instance->Id;

        // Transient: the authored persistent id never reaches the world.
        EXPECT_EQ(world.TryGet<PersistentIdComponent>(entity), nullptr);
    }
    EXPECT_FALSE(world.GetResource<PersistentEntityIndex>()
                     .TryResolve(PersistentEntityId{ 0x77 })
                     .IsValid());

    // The root composed the placement: authored (1,2,3) spawned at x+10.
    bool sawRoot = false;
    for (EntityId entity : members)
    {
        if (world.TryGet<Parent>(entity) != nullptr)
            continue;
        const LocalTransform* local = world.TryGet<LocalTransform>(entity);
        ASSERT_NE(local, nullptr);
        EXPECT_FLOAT_EQ(local->Value.Position.X, 11.0f);
        EXPECT_FLOAT_EQ(local->Value.Position.Y, 2.0f);
        EXPECT_FLOAT_EQ(local->Value.Position.Z, 3.0f);
        sawRoot = true;
    }
    EXPECT_TRUE(sawRoot);

    // The scene reference was scaffolding; residency has been released.
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
    (void)group;
}

TEST(SceneSpawnService, PublishesInRequestOrderWhenBothAreReady)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "pair");

    const SceneSpawnId first =
        h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    const SceneSpawnId second =
        h.Service.RequestSpawn(scene.Path, Transform3f::Identity());

    // Both workers finish before one pump; publication must still allocate
    // the first request's entities first.
    h.Turn();
    ASSERT_EQ(h.Service.Status(first), SceneSpawnStatus::Live);
    ASSERT_EQ(h.Service.Status(second), SceneSpawnStatus::Live);

    const std::span<const EntityId> a = h.Service.Entities(first);
    const std::span<const EntityId> b = h.Service.Entities(second);
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    for (EntityId earlier : a)
        for (EntityId later : b)
            EXPECT_LT(earlier.Index, later.Index);
}

TEST(SceneSpawnService, AFailedHeadDoesNotWedgeTheQueue)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "tail");

    // The head request resolves but its file is gone: it fails at staging.
    EXPECT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::Scene,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://scenes/gone.smap",
        .FilePath = "does/not/exist.smap",
    }));
    const SceneSpawnId doomed =
        h.Service.RequestSpawn("asset://scenes/gone.smap", Transform3f::Identity());
    const SceneSpawnId tail =
        h.Service.RequestSpawn(scene.Path, Transform3f::Identity());

    const std::size_t aliveBefore =
        h.World.Entities().GetAliveEntities().size();
    h.Turn();

    EXPECT_EQ(h.Service.Status(doomed), SceneSpawnStatus::Failed);
    EXPECT_EQ(h.Service.Status(tail), SceneSpawnStatus::Live);
    EXPECT_TRUE(h.Service.Entities(doomed).empty());
    // The failure created nothing; only the tail's two entities exist.
    EXPECT_EQ(h.World.Entities().GetAliveEntities().size(), aliveBefore + 2u);
}

TEST(SceneSpawnService, DespawnDestroysTheGroupAndIndividualDestroyPrunesIt)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "prune");

    const SceneSpawnId id =
        h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    h.Turn();
    ASSERT_EQ(h.Service.Status(id), SceneSpawnStatus::Live);
    ASSERT_EQ(h.Service.Entities(id).size(), 2u);

    // Gameplay destroys one member: the group shrinks through the component
    // hooks, with no sweep and no service involvement.
    h.World.Entities().DestroyEntity(h.Service.Entities(id)[1]);
    EXPECT_EQ(h.Service.Entities(id).size(), 1u);
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Live);

    ASSERT_TRUE(h.Service.RequestDespawn(id));
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Live); // until the pump
    h.Service.Pump();
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Despawned);
    EXPECT_TRUE(h.Service.Entities(id).empty());
    EXPECT_FALSE(h.Service.RequestDespawn(id));
}

TEST(SceneSpawnService, RefusalsAreStatusesNotCrashes)
{
    SpawnHarness h;

    // Unresolvable path.
    const SceneSpawnId unknown =
        h.Service.RequestSpawn("asset://scenes/never.smap", Transform3f::Identity());
    EXPECT_EQ(h.Service.Status(unknown), SceneSpawnStatus::Failed);

    // No asset system connected.
    h.Service.ConnectAssets(nullptr);
    const SceneSpawnId unwired =
        h.Service.RequestSpawn("asset://scenes/never.smap", Transform3f::Identity());
    EXPECT_EQ(h.Service.Status(unwired), SceneSpawnStatus::Failed);

    // A made-up id.
    EXPECT_EQ(h.Service.Status(SceneSpawnId{ 999 }), SceneSpawnStatus::Unknown);
    EXPECT_FALSE(h.Service.RequestDespawn(SceneSpawnId{ 999 }));
}
