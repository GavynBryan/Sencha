// A projectile launched on the authority reaches a client as the prefab it was
// built from, carrying who launched it as the client knows that entity and the
// ticks it flew between, and goes when the authority lets it go. The client
// never receives, and never runs, the authority's working state.

#include <gtest/gtest.h>

#include "StubPrefabSpawner.h"

#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationInterpolation.h>
#include <net/ReplicationSnapshot.h>
#include <physics/PhysicsWorld.h>
#include <projectile/Projectile.h>
#include <projectile/ProjectileState.h>
#include <projectile/ProjectileSystem.h>
#include <render/StaticMeshComponent.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <span>
#include <vector>

namespace
{
constexpr std::size_t kSnapshotBytes = 64 * 1024;
const AssetId kBolt{ 0xB017 };

std::vector<EntityId> BuildBolt(World& world, StoragePartitionId partition)
{
    Projectile projectile;
    projectile.Speed = 10.0f;
    projectile.Lifetime = 0.3f;
    const EntityId root = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(root, LocalTransform{});
    world.AddComponent<WorldTransform>(root, WorldTransform{});
    world.AddComponent<StaticMeshComponent>(root, StaticMeshComponent{});
    world.AddComponent<Projectile>(root, projectile);
    return { root };
}

// Two worlds from one sealed schema, with a snapshot carried between them as
// bytes; see ReplicationSnapshotTests for the harness this is the shape of.
struct Pair
{
    WorldComponentSchema Schema;
    ReplicationLayout Layout;
    World Authority;
    World Client;
    ReplicationAuthorityIdentity Identity;
    ReplicationChangeStore Changes;
    std::uint64_t Generation = 0;
    ReplicationPeerState Peer;
    ReplicationClientIdentity ClientIdentity;
    NetSnapshotAck ClientAck;
    std::vector<std::byte> Scratch = std::vector<std::byte>(kSnapshotBytes);

    Pair()
    {
        ComponentRegistrar components(&Schema, nullptr, &Layout);
        RegisterEngineComponents(components);
        Schema.Seal();
        Schema.Apply(Authority);
        Schema.Apply(Client);
        Layout.Seal();
    }

    void Replicate(std::uint64_t tick, INetPrefabSpawner& prefabs, ReplicationInterpolation& interpolation)
    {
        Changes.Update(Authority, Layout, Identity, ++Generation);
        SnapshotWriteRequest write;
        write.Changes = &Changes;
        write.Layout = &Layout;
        write.Peer = &Peer;
        write.Tick = tick;
        write.Sequence = Peer.NextSnapshotSequence();
        const SnapshotWriteResult written = ReplicationWriteSnapshot(write, std::span(Scratch));
        ASSERT_TRUE(written.Ok);

        SnapshotApplyRequest apply;
        apply.Target = &Client;
        apply.Schema = &Schema;
        apply.Layout = &Layout;
        apply.Identity = &ClientIdentity;
        apply.Prefabs = &prefabs;
        apply.Interpolation = &interpolation;
        const SnapshotApplyResult applied =
            ReplicationApplySnapshot(apply, std::span(Scratch).subspan(0, written.BytesWritten));
        ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);
        if (applied.Complete())
        {
            ClientAck.Observe(applied.Sequence);
            Peer.Acknowledge(ClientAck);
        }
    }

    [[nodiscard]] EntityId Mirror(EntityId authorityEntity) const
    {
        const NetEntityId id = Identity.TryFind(authorityEntity);
        return id.IsValid() ? ClientIdentity.TryResolve(id) : EntityId{};
    }
};
} // namespace

TEST(ProjectileReplication, AClientBuildsTheProjectilesPrefabAndKnowsWhoLaunchedItAndWhenItFlew)
{
    Pair pair;
    StubPrefabSpawner authorityPrefabs;
    StubPrefabSpawner clientPrefabs;
    authorityPrefabs.Register(kBolt, BuildBolt);
    clientPrefabs.Register(kBolt, BuildBolt);
    ReplicationInterpolation interpolation;
    PhysicsWorld physics;
    ProjectileSystem system(pair.Authority,
                            ProjectileSystem::Services{ .Physics = &physics, .Prefabs = &authorityPrefabs });
    system.SetLingerTicks(2);

    const EntityId shooter = pair.Authority.CreateEntity();
    pair.Authority.AddComponent<NetReplicated>(shooter);
    pair.Authority.AddComponent<LocalTransform>(shooter, LocalTransform{});

    EngineConfig config;
    RuntimeFrameLoop runtime;
    StoragePartitionSet logic;
    logic.Add(StoragePartitionId::Default());
    auto tick = [&](std::uint64_t index) {
        FixedLogicContext fixed{ .Config = config,
                                 .Runtime = runtime,
                                 .Time = FixedSimTime{ .DeltaSeconds = 0.1, .TickIndex = index },
                                 .Entities = pair.Authority,
                                 .Partitions = logic };
        system.FixedLogic(fixed);
    };

    const EntityId bolt = system.Launch(ProjectileLaunch{ .Prefab = kBolt,
                                                          .Instigator = shooter,
                                                          .Direction = Vec3d(1.0f, 0.0f, 0.0f),
                                                          .Tick = 1 });
    ASSERT_TRUE(bolt.IsValid());
    tick(1);
    pair.Replicate(1, clientPrefabs, interpolation);

    const EntityId mirror = pair.Mirror(bolt);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_TRUE(pair.Client.HasComponent<Projectile>(mirror)) << "built from the prefab the authority named";
    EXPECT_TRUE(pair.Client.HasComponent<StaticMeshComponent>(mirror)) << "with the body the prefab gives it";
    const ProjectileFlight* flight = pair.Client.TryGet<ProjectileFlight>(mirror);
    ASSERT_NE(flight, nullptr);
    EXPECT_EQ(flight->Instigator, pair.Mirror(shooter)) << "its instigator, as this machine knows that entity";
    EXPECT_EQ(flight->LaunchTick, 1u);
    EXPECT_EQ(flight->EndTick, 0u);
    EXPECT_FALSE(pair.Client.HasComponent<ProjectileMotion>(mirror)) << "the authority's working state stays there";

    tick(2);
    tick(3);
    pair.Replicate(3, clientPrefabs, interpolation);
    EXPECT_EQ(pair.Client.TryGet<ProjectileFlight>(mirror)->EndTick, 3u) << "its end reaches the client";

    tick(4);
    tick(5);
    pair.Replicate(5, clientPrefabs, interpolation);
    EXPECT_FALSE(pair.Authority.IsAlive(bolt));
    EXPECT_FALSE(pair.Client.IsAlive(mirror)) << "and it goes when the authority lets it go";
}
