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
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <movement/MovementComponentAssets.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <movement/MovementRegistration.h>
#include <physics/components/CharacterController.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

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
                std::filesystem::path(SENCHA_REPO_ROOT) / "template/assets";
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
            // be what a spawn carries.
            AssetIdMap idMap;
            std::string error;
            ASSERT_TRUE(AssetIdMap::LoadFromFile(
                (root / std::string(kAssetIdMapFileName)).generic_string(), idMap,
                &error)) << error;
            (void)ApplyAssetIds(idMap, Assets->Registry);

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
            // against, and where the movement profile it names is held. A
            // runtime composes both; without them the prefab's tags,
            // attributes, and tuning would refuse to load.
            RegisterMovement(Runtime->Entities());
            Runtime->Entities().GetResource<MovementComponentAssets>().Profiles =
                &Assets->DataAssets;
            Spawner.emplace(*Runtime, Schema, Serializers, Logging);
            Spawner->ConnectAssets(&Assets->Assets);
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

// A process with no content stack refuses every prefab rather than producing
// bodyless entities. A dedicated host composed without graphics is not this --
// it still has scenes -- but a bare test world is.
TEST_F(NetPrefabSpawnerTest, WithNoAssetSystemEveryPrefabIsUnavailable)
{
    Spawner->ConnectAssets(nullptr);
    EXPECT_EQ(Spawner->Prepare(PawnId), NetPrefabReadiness::Unavailable);
    EXPECT_FALSE(
        Spawner->Instantiate(PawnId, Runtime->Entities(), PersistentStoragePartition)
            .IsValid());
}
