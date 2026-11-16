// Building a replicated body from the prefab identity the authority named.
//
// The applier's side of this is covered against a stub (ReplicationSnapshot
// tests); what is here is the half that touches real content: an id resolves to
// a scene or it does not, a scene has one root or it does not, and an entity
// either comes out whole or nothing comes out at all.
//
// Every refusal matters more than the success. A prefab that will not build has
// to be refused rather than approximated, because the approximation -- an
// entity with its state and no body -- is invisible in the literal sense.

#include <runtime/spawn/NetPrefabSpawner.h>

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <movement/MovementRegistration.h>
#include <physics/components/CharacterController.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include "SceneSpawnHarness.h"
#include "SmapSceneFixture.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kPawnPrefab = "asset://prefabs/player_pawn.smap";

    class NetPrefabSpawnerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const std::filesystem::path root =
                std::filesystem::path(SENCHA_REPO_ROOT) / "templates/fps/assets";
            if (!std::filesystem::exists(root / ".cooked/prefabs/player_pawn.smap"))
                GTEST_SKIP() << "the template prefabs are not cooked here";

            RegisterEngineSceneSerializers(Serializers);
            Assets.emplace(Logging, Serializers);
            // Two roots, as a shipping player composes them: the authored
            // tree, then the cooked one whose artifacts keep the virtual paths
            // the cook gave them.
            (void)ScanAssetsDirectory(root.generic_string(), Assets->Registry,
                                      Assets->Assets.Kinds());
            (void)ScanAssetsDirectory((root / ".cooked").generic_string(),
                                      Assets->Registry, Assets->Assets.Kinds());
            (void)RegisterCookedAssets(root.generic_string(), Assets->Registry);

            // The ids both machines share, which is the whole reason an id can
            // be what a spawn carries: the cooked index names every artifact's.
            const AssetRecord* pawn =
                Assets->Registry.FindByPath(std::string(kPawnPrefab));
            ASSERT_NE(pawn, nullptr);
            PawnId = pawn->Id;
            ASSERT_TRUE(PawnId.IsValid())
                << "the cooked prefab has no stable id, so nothing could name it";

            ComponentRegistrar components(&Schema, nullptr, nullptr);
            RegisterEngineComponents(components);
            Schema.Seal();
            Runtime.emplace(Schema);
            // The vocabulary the prefab's name-based components resolve
            // against, and the stores that hold the movement profile it
            // names. A runtime composes both; without them the prefab's tags,
            // attributes, and tuning would refuse to load.
            RegisterMovement(Runtime->Entities());
            Runtime->Entities().SetResource(Assets->Assets.Stores());
            Spawner.emplace(Schema, Serializers, Logging);
            Spawner->ConnectAssets(&Assets->Assets, &Assets->Scenes);
        }

        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::optional<RuntimeAssets> Assets;
        WorldComponentSchema Schema;
        std::optional<RuntimeWorld> Runtime;
        std::optional<NetPrefabSpawner> Spawner;
        AssetId PawnId;
    };
}

TEST_F(NetPrefabSpawnerTest, ThePawnPrefabResolvesAndBuildsAWholePawn)
{
    ASSERT_EQ(Spawner->Prepare(PawnId), NetPrefabReadiness::Ready);

    World& world = Runtime->Entities();
    const EntityId root =
        Spawner->Instantiate(PawnId, world, PersistentStoragePartition);
    ASSERT_TRUE(root.IsValid()) << "the prefab resolved and then built nothing";

    // The archetype the authority is simulating, arrived without anyone
    // listing its components.
    EXPECT_TRUE(world.HasComponent<LocalTransform>(root));
    EXPECT_TRUE(world.HasComponent<CharacterController>(root));
    EXPECT_TRUE(world.HasComponent<CharacterMovement>(root));
    EXPECT_TRUE(world.HasComponent<MovementTuningSource>(root));
    // And the per-tick scratch the movement component owes.
    EXPECT_TRUE(world.HasComponent<MotionRequest>(root));
    EXPECT_TRUE(world.HasComponent<ResolvedMovementTuning>(root));

    // Its group identity, which is what a later teardown addresses it by.
    const SceneInstance* group = world.TryGet<SceneInstance>(root);
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->Source, PawnId);
}

// A prefab resolved once stays resolved, and each spawn is its own group: two
// players' pawns must not be one entity's worth of anything.
TEST_F(NetPrefabSpawnerTest, EachSpawnIsItsOwnGroup)
{
    ASSERT_EQ(Spawner->Prepare(PawnId), NetPrefabReadiness::Ready);
    World& world = Runtime->Entities();

    const EntityId first =
        Spawner->Instantiate(PawnId, world, PersistentStoragePartition);
    const EntityId second =
        Spawner->Instantiate(PawnId, world, PersistentStoragePartition);
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    EXPECT_NE(first, second);
    EXPECT_NE(world.TryGet<SceneInstance>(first)->Id,
              world.TryGet<SceneInstance>(second)->Id)
        << "two spawns share a group, so tearing one down would take the other";

    Spawner->Despawn(world, first);
    EXPECT_FALSE(world.IsAlive(first));
    EXPECT_TRUE(world.IsAlive(second))
        << "despawning one player's body took another player's with it";
}

// The wire names only a prefab body's root; the despawn it triggers on a
// client has to sweep the children the same prefab built there.
TEST_F(NetPrefabSpawnerTest, DespawningTheRootRemovesTheWholeGroup)
{
    ASSERT_EQ(Spawner->Prepare(PawnId), NetPrefabReadiness::Ready);
    World& world = Runtime->Entities();
    const EntityId root =
        Spawner->Instantiate(PawnId, world, PersistentStoragePartition);
    ASSERT_TRUE(root.IsValid());
    const SceneInstanceId group = world.TryGet<SceneInstance>(root)->Id;
    const SceneInstanceIndex& index = world.GetResource<SceneInstanceIndex>();
    ASSERT_GE(index.Entities(group).size(), 2u) << "the pawn prefab has a child";

    Spawner->Despawn(world, root);

    EXPECT_TRUE(index.Entities(group).empty());
}

// An id no scene in this build answers to. The authority is running content
// this machine does not have, which is a real thing to be told about -- and
// told once, not every snapshot.
TEST_F(NetPrefabSpawnerTest, AnUnknownIdIsRefusedRatherThanApproximated)
{
    const AssetId absent{ 0xDEADBEEFCAFEull };
    EXPECT_EQ(Spawner->Prepare(absent), NetPrefabReadiness::Unavailable);
    EXPECT_EQ(Spawner->RefusedCount(), 1u);

    // Asked again, as a peer resending the same spawn would: still refused,
    // still one complaint.
    EXPECT_EQ(Spawner->Prepare(absent), NetPrefabReadiness::Unavailable);
    EXPECT_EQ(Spawner->RefusedCount(), 1u);

    // And it builds nothing, so no half-entity is left behind.
    const std::size_t before = Runtime->Entities().GetAliveEntities().size();
    EXPECT_FALSE(
        Spawner->Instantiate(absent, Runtime->Entities(), PersistentStoragePartition)
            .IsValid());
    EXPECT_EQ(Runtime->Entities().GetAliveEntities().size(), before);
}

// A resolved prefab is held for the session, and the reference belongs to a
// cache the spawner does not own. Disconnecting is how a host gives those back
// before its asset stack goes away, so the release has to happen there rather
// than in a destructor that runs whenever the engine happens to go.
TEST_F(NetPrefabSpawnerTest, DisconnectingGivesBackTheScenesItHeld)
{
    ASSERT_EQ(Spawner->Prepare(PawnId), NetPrefabReadiness::Ready);
    ASSERT_TRUE(Assets->Scenes.Find(kPawnPrefab).IsValid());

    Spawner->ConnectAssets(nullptr, nullptr);
    EXPECT_EQ(Spawner->ResolvedCount(), 0u);
    EXPECT_FALSE(Assets->Scenes.Find(kPawnPrefab).IsValid())
        << "the prefab's scene outlived the connection that resolved it";
}

// A process with no content stack refuses every prefab rather than producing
// bodyless entities. A dedicated host composed without graphics is not this --
// it still has scenes -- but a bare test world is.
TEST_F(NetPrefabSpawnerTest, WithNoAssetSystemEveryPrefabIsUnavailable)
{
    Spawner->ConnectAssets(nullptr, nullptr);
    EXPECT_EQ(Spawner->Prepare(PawnId), NetPrefabReadiness::Unavailable);
    EXPECT_FALSE(
        Spawner->Instantiate(PawnId, Runtime->Entities(), PersistentStoragePartition)
            .IsValid());
}

// A prefab that places another cooks its placed members, root included, with
// the placement's authored instance id. The spawned root must still name the
// spawned prefab: that is the identity a host replicates to its peers.
TEST(NetPrefabSpawnerGroup, APlacedRootNamesTheSpawnedPrefab)
{
    SpawnHarness h;
    const auto placed = [](SmapEntityRecord& record, double marker) {
        record.Components.emplace_back(MakeComponentTypeId("spawn_marker"),
                                       JsonValue(JsonValue::Object{ { "value", JsonValue(marker) } }));
        record.Components.emplace_back(
            MakeComponentTypeId("scene_instance"),
            JsonValue(JsonValue::Object{
                { "source", JsonValue(std::string("asset://prefabs/bulb.sscene")) },
                { "id", JsonValue(std::string("00000000000000f0")) } }));
    };
    SmapContents contents;
    SmapEntityRecord root;
    placed(root, 1.0);
    contents.Entities.push_back(std::move(root));
    SmapEntityRecord child;
    child.Parent = 0;
    placed(child, 2.0);
    contents.Entities.push_back(std::move(child));
    const AssetId sceneId{ 0x5EEDu };
    TempSmapScene scene(h.Registry, h.Serializers, contents, "net_variant", sceneId);

    NetPrefabSpawner spawner(h.Schema, h.Serializers, h.Logging);
    spawner.ConnectAssets(&h.Assets, &h.Scenes);
    ASSERT_EQ(spawner.Prepare(sceneId), NetPrefabReadiness::Ready);

    World& world = h.World.Entities();
    const EntityId spawned = spawner.Instantiate(sceneId, world, PersistentStoragePartition);
    ASSERT_TRUE(spawned.IsValid());
    const SceneInstance* group = world.TryGet<SceneInstance>(spawned);
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->Source, sceneId) << "the root names the placed prefab, which peers would be told to spawn";
    EXPECT_NE(group->Id.Value & SceneInstanceIdRuntimeBit, 0u);

    spawner.Despawn(world, spawned);
    EXPECT_TRUE(world.GetAliveEntities().empty());
    spawner.ConnectAssets(nullptr, nullptr);
}

namespace
{
    // A prefab of three: a root, a materialized child, and the child's own child.
    SmapContents RootMaterializedChildGrandchild(bool materializedRoot)
    {
        const auto component = [](std::string_view name, double value) {
            return std::pair{ MakeComponentTypeId(name), JsonValue(JsonValue::Object{ { "value", JsonValue(value) } }) };
        };
        SmapContents contents;
        SmapEntityRecord root;
        root.Components.push_back(component(materializedRoot ? "materialized_marker" : "spawn_marker", 1.0));
        contents.Entities.push_back(std::move(root));
        SmapEntityRecord child;
        child.Parent = 0;
        child.Components.push_back(component(materializedRoot ? "spawn_marker" : "materialized_marker", 2.0));
        contents.Entities.push_back(std::move(child));
        SmapEntityRecord grandchild;
        grandchild.Parent = 1;
        grandchild.Components.push_back(component("spawn_marker", 3.0));
        contents.Entities.push_back(std::move(grandchild));
        return contents;
    }

    std::size_t Alive(const World& world) { return world.GetAliveEntities().size(); }
}

// A client built every member of a net-spawned prefab, so a member the authority
// creates and replicates on its own arrived twice. A client skips what only the
// authority materializes, and everything beneath it.
TEST(NetPrefabSpawnerGroup, AClientSkipsWhatOnlyTheAuthorityMaterializes)
{
    SpawnHarness h;
    const AssetId sceneId{ 0x5EED1u };
    TempSmapScene scene(h.Registry, h.Serializers, RootMaterializedChildGrandchild(false), "materialized_child", sceneId);
    NetPrefabSpawner spawner(h.Schema, h.Serializers, h.Logging);
    spawner.ConnectAssets(&h.Assets, &h.Scenes);
    ASSERT_EQ(spawner.Prepare(sceneId), NetPrefabReadiness::Ready);
    World& world = h.World.Entities();

    world.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    const EntityId root = spawner.Instantiate(sceneId, world, PersistentStoragePartition);
    ASSERT_TRUE(root.IsValid()) << "the root is still the client's to build";
    EXPECT_EQ(Alive(world), 1u) << "the materialized child and its child were built anyway";
    EXPECT_EQ(world.GetResource<SceneInstanceIndex>().Entities(world.TryGet<SceneInstance>(root)->Id).size(), 1u)
        << "the group lists only what was built";
    spawner.Despawn(world, root);
    EXPECT_EQ(Alive(world), 0u);

    world.SetResource(SimulationAuthority{ .Authoritative = true, .TickOffset = 0 });
    const EntityId whole = spawner.Instantiate(sceneId, world, PersistentStoragePartition);
    ASSERT_TRUE(whole.IsValid());
    EXPECT_EQ(Alive(world), 3u) << "the authority builds every member";
    spawner.Despawn(world, whole);
    spawner.ConnectAssets(nullptr, nullptr);
}

// A prefab whose root only the authority materializes builds nothing of its own
// on a client: the wire binds to a bare entity, as it would to any spawn.
TEST(NetPrefabSpawnerGroup, AMaterializedRootIsABareEntityOnAClient)
{
    SpawnHarness h;
    const AssetId sceneId{ 0x5EED2u };
    TempSmapScene scene(h.Registry, h.Serializers, RootMaterializedChildGrandchild(true), "materialized_root", sceneId);
    NetPrefabSpawner spawner(h.Schema, h.Serializers, h.Logging);
    spawner.ConnectAssets(&h.Assets, &h.Scenes);
    ASSERT_EQ(spawner.Prepare(sceneId), NetPrefabReadiness::Ready);
    World& world = h.World.Entities();
    world.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });

    const EntityId root = spawner.Instantiate(sceneId, world, PersistentStoragePartition);
    ASSERT_TRUE(root.IsValid()) << "a spawn the wire names must bind to something";
    EXPECT_EQ(Alive(world), 1u);
    EXPECT_FALSE(world.HasComponent<SpawnMarker>(root));
    EXPECT_FALSE(world.HasComponent<MaterializedMarker>(root));
    spawner.Despawn(world, root);
    EXPECT_EQ(Alive(world), 0u);
    spawner.ConnectAssets(nullptr, nullptr);
}
