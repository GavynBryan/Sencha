// SceneSpawnService: request -> task-lane stage/build -> owner-thread pump,
// publishing in request order, group-addressable through SceneInstanceIndex,
// transient (authored persistent identity stripped). Zero-thread, so every
// stage is deterministic.

#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceGroup.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include "SmapSceneFixture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
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
    h.Service.ConnectAssets(nullptr, nullptr);
    const SceneSpawnId unwired =
        h.Service.RequestSpawn("asset://scenes/never.smap", Transform3f::Identity());
    EXPECT_EQ(h.Service.Status(unwired), SceneSpawnStatus::Failed);

    // A made-up id.
    EXPECT_EQ(h.Service.Status(SceneSpawnId{ 999 }), SceneSpawnStatus::Unknown);
    EXPECT_FALSE(h.Service.RequestDespawn(SceneSpawnId{ 999 }));
}

// Whoever started a request can always end it, including before anything
// exists. A withdrawn request finishes staging and is then discarded rather
// than published, so a body requested for a participant who left never
// arrives to stand around with no owner.
TEST(SceneSpawnService, APendingRequestCanBeWithdrawnAndIsNeverPublished)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "lamp");

    const SceneSpawnId id = h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    ASSERT_EQ(h.Service.Status(id), SceneSpawnStatus::Pending);

    EXPECT_TRUE(h.Service.RequestDespawn(id)) << "a pending request refused to end";
    EXPECT_FALSE(h.Service.RequestDespawn(id)) << "ending it twice is not a second end";

    h.Turn();
    h.Turn();

    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Despawned);
    EXPECT_TRUE(h.Service.Entities(id).empty());
    EXPECT_TRUE(h.World.Entities().GetAliveEntities().empty())
        << "a withdrawn spawn published its group anyway";
}

// Status reports the pump's view: a queued despawn is Live until the pump
// runs, and a withdrawn request is Pending. Whoever holds a group open for a
// handoff needs to know the request has been ended before then.
TEST(SceneSpawnService, ADespawnRequestIsObservableBeforeThePump)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "ending");

    EXPECT_FALSE(h.Service.IsDespawnRequested(SceneSpawnId{ 999 }));

    const SceneSpawnId withdrawn =
        h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    EXPECT_FALSE(h.Service.IsDespawnRequested(withdrawn));
    ASSERT_TRUE(h.Service.RequestDespawn(withdrawn));
    EXPECT_TRUE(h.Service.IsDespawnRequested(withdrawn));
    EXPECT_EQ(h.Service.Status(withdrawn), SceneSpawnStatus::Pending);

    const SceneSpawnId live =
        h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    h.Turn();
    ASSERT_EQ(h.Service.Status(live), SceneSpawnStatus::Live);
    EXPECT_FALSE(h.Service.IsDespawnRequested(live));
    ASSERT_TRUE(h.Service.RequestDespawn(live));
    EXPECT_TRUE(h.Service.IsDespawnRequested(live));
    EXPECT_EQ(h.Service.Status(live), SceneSpawnStatus::Live);
    h.Turn();
    EXPECT_TRUE(h.Service.IsDespawnRequested(live));
    EXPECT_EQ(h.Service.Status(live), SceneSpawnStatus::Despawned);
    // A withdrawal outlives the discard the pump performed.
    EXPECT_TRUE(h.Service.IsDespawnRequested(withdrawn));
}

// A withdrawn request whose build then fails is still an ended request: the
// withdrawal is what the holder acted on, and the failure changes nothing.
TEST(SceneSpawnService, AWithdrawalSurvivesTheBuildFailing)
{
    SpawnHarness h;
    EXPECT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::Scene,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://scenes/gone.smap",
        .FilePath = "does/not/exist.smap",
    }));
    const SceneSpawnId doomed =
        h.Service.RequestSpawn("asset://scenes/gone.smap", Transform3f::Identity());
    ASSERT_TRUE(h.Service.RequestDespawn(doomed));
    h.Turn();
    EXPECT_NE(h.Service.Status(doomed), SceneSpawnStatus::Live);
    EXPECT_TRUE(h.Service.IsDespawnRequested(doomed));
}

namespace
{
    [[nodiscard]] SmapContents MakeTwoRootContents()
    {
        SmapContents contents = MakeSpawnContents();
        contents.Entities[1].Parent = UINT32_MAX;
        return contents;
    }

    [[nodiscard]] SceneSpawnId SpawnOwned(SpawnHarness& h, const std::string& path)
    {
        return h.Service.RequestSpawn(path, Transform3f::Identity(),
                                      StoragePartitionId::Default(),
                                      SceneSpawnLifetime::OwnedByRoot);
    }
}

// The default keeps what a spawn always was: members outlive one another.
TEST(SceneSpawnService, AnIndependentGroupOutlivesItsRoot)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "loose");
    const SceneSpawnId id = h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    h.Turn();
    const EntityId root = h.Service.Root(id);
    ASSERT_TRUE(root.IsValid());

    h.World.Entities().DestroyEntity(root);
    h.Turn();
    h.Turn();

    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Live);
    EXPECT_FALSE(h.Service.IsDespawnRequested(id));
    EXPECT_EQ(h.Service.Entities(id).size(), 1u);
}

TEST(SceneSpawnService, ARootOwnedGroupFollowsItsRootAtTheNextPump)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "owned");
    const SceneSpawnId id = SpawnOwned(h, scene.Path);
    h.Turn();
    ASSERT_EQ(h.Service.Status(id), SceneSpawnStatus::Live);
    const EntityId root = h.Service.Root(id);
    ASSERT_TRUE(root.IsValid());

    h.World.Entities().DestroyEntity(root);
    EXPECT_EQ(h.Service.Entities(id).size(), 1u) << "nothing happens inside the destroy";
    EXPECT_FALSE(h.Service.Root(id).IsValid());

    h.Service.Pump();
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Despawned);
    EXPECT_TRUE(h.Service.IsDespawnRequested(id));
    EXPECT_TRUE(h.World.Entities().GetAliveEntities().empty());
}

TEST(SceneSpawnService, ARootOwnedSpawnNeedsOneRootAndPublishesNothingOtherwise)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeTwoRootContents(), "pair");
    const SceneSpawnId owned = SpawnOwned(h, scene.Path);
    const SceneSpawnId loose = h.Service.RequestSpawn(scene.Path, Transform3f::Identity());
    h.Turn();

    EXPECT_EQ(h.Service.Status(owned), SceneSpawnStatus::Failed);
    EXPECT_TRUE(h.Service.Entities(owned).empty());
    EXPECT_EQ(h.Service.Status(loose), SceneSpawnStatus::Live);
    EXPECT_EQ(h.World.Entities().GetAliveEntities().size(), 2u)
        << "only the independent spawn may publish a two-root scene";
}

TEST(SceneSpawnService, OnlyTheGroupWhoseRootDiedIsDespawned)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "both");
    const SceneSpawnId first = SpawnOwned(h, scene.Path);
    const SceneSpawnId second = SpawnOwned(h, scene.Path);
    h.Turn();

    h.World.Entities().DestroyEntity(h.Service.Root(second));
    h.Service.Pump();

    EXPECT_EQ(h.Service.Status(first), SceneSpawnStatus::Live);
    EXPECT_EQ(h.Service.Entities(first).size(), 2u);
    EXPECT_EQ(h.Service.Status(second), SceneSpawnStatus::Despawned);
}

TEST(SceneSpawnService, RootNamesThePublishedSingleRootOnly)
{
    SpawnHarness h;
    TempSmapScene single(h.Registry, h.Serializers, MakeSpawnContents(), "single");
    TempSmapScene pair(h.Registry, h.Serializers, MakeTwoRootContents(), "pair");

    const SceneSpawnId id = h.Service.RequestSpawn(single.Path, Transform3f::Identity());
    const SceneSpawnId twoRoots = h.Service.RequestSpawn(pair.Path, Transform3f::Identity());
    EXPECT_FALSE(h.Service.Root(id).IsValid()) << "pending";
    EXPECT_FALSE(h.Service.Root(SceneSpawnId{ 999 }).IsValid());
    h.Turn();

    const EntityId root = h.Service.Root(id);
    ASSERT_TRUE(root.IsValid());
    EXPECT_EQ(h.World.Entities().TryGet<Parent>(root), nullptr);
    EXPECT_EQ(h.Service.Status(twoRoots), SceneSpawnStatus::Live);
    EXPECT_FALSE(h.Service.Root(twoRoots).IsValid()) << "two roots name no root";

    // A member unparented later is a second parentless entity, not a new root.
    for (const EntityId member : h.Service.Entities(id))
        if (member != root)
            h.World.Entities().RemoveComponent<Parent>(member);
    EXPECT_EQ(h.Service.Root(id), root);

    h.World.Entities().DestroyEntity(root);
    EXPECT_FALSE(h.Service.Root(id).IsValid());
}

TEST(SceneSpawnService, AnExplicitDespawnOfARootOwnedGroupHappensOnce)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "ended");
    const SceneSpawnId id = SpawnOwned(h, scene.Path);
    h.Turn();

    ASSERT_TRUE(h.Service.RequestDespawn(id));
    h.Service.Pump();
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Despawned);
    EXPECT_FALSE(h.Service.RequestDespawn(id));
    h.Service.Pump();
    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Despawned);
    EXPECT_TRUE(h.World.Entities().GetAliveEntities().empty());
}

// A partition teardown takes every member at once; the group settles quietly.
TEST(SceneSpawnService, ARootOwnedGroupWhoseMembersAllWentSettlesAsDespawned)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "swept");
    const SceneSpawnId id = SpawnOwned(h, scene.Path);
    h.Turn();

    const std::span<const EntityId> members = h.Service.Entities(id);
    const std::vector<EntityId> all(members.begin(), members.end());
    for (const EntityId entity : all)
        h.World.Entities().DestroyEntity(entity);
    h.Service.Pump();

    EXPECT_EQ(h.Service.Status(id), SceneSpawnStatus::Despawned);
}

// A client's replicated groups arrive through the net prefab spawner, so no
// record here names them: root-owned lifetime cannot destroy what the
// authority replicates. Only the snapshot's own despawn removes such a group.
TEST(SceneSpawnService, RootOwnershipNeverReachesAGroupThisServiceDidNotSpawn)
{
    SpawnHarness h;
    TempSmapScene scene(h.Registry, h.Serializers, MakeSpawnContents(), "local");
    const SceneSpawnId owned = SpawnOwned(h, scene.Path);
    h.Turn();

    World& world = h.World.Entities();
    const SceneInstanceId replicatedGroup{ SceneInstanceIdRuntimeBit | 0xABCDu };
    const EntityId replicatedRoot = world.CreateEntity();
    world.AddComponent<SceneInstance>(replicatedRoot, SceneInstance{ {}, replicatedGroup });
    const EntityId replicatedChild = world.CreateEntity();
    world.AddComponent<SceneInstance>(replicatedChild, SceneInstance{ {}, replicatedGroup });
    world.AddComponent<Parent>(replicatedChild, Parent{ replicatedRoot });

    world.DestroyEntity(replicatedRoot);
    world.DestroyEntity(h.Service.Root(owned));
    h.Turn();

    EXPECT_TRUE(world.IsAlive(replicatedChild)) << "a local policy destroyed a replicated member";
    EXPECT_EQ(h.Service.Status(owned), SceneSpawnStatus::Despawned);

    DestroySceneInstance(world, replicatedGroup);
    EXPECT_FALSE(world.IsAlive(replicatedChild));
}
