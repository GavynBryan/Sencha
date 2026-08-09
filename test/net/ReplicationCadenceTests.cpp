#include <gtest/gtest.h>

#include <core/metadata/Field.h>
#include <ecs/WorldComponentSchema.h>
#include <net/LoopbackTransport.h>
#include <net/NetSession.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationRuntime.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <controller/LookOrientation.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <vector>

//=============================================================================
// How often the authority speaks.
//
// The rate a snapshot is worth sending at is set by how fast a player can
// perceive a change, not by how fast the world is stepped -- and the cost of a
// snapshot is paid once per peer, so this is the term that decides how many
// players fit in a session. What is tested here is the pacing itself: that it
// holds under a frame rate above and below the tick rate, and that a client is
// told the cadence rather than left to assume it.
//=============================================================================

namespace
{
    NetIdentity SampleIdentity()
    {
        return NetIdentity{
            .ModuleFingerprint = 0xABCDEF,
            .ReplicationTableHash = 0xFEDCBA,
            .WorldIdentity = 0x123456,
            .FixedTickRateMilliHz = 60000,
        };
    }

    // A host with one connected peer and one replicated entity: the smallest
    // world that produces a snapshot at all.
    struct PublishRig
    {
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport ClientTransport{ Network };
        NetSession Host{ HostTransport };
        NetSession Client{ ClientTransport };

        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Entities;
        ReplicationRuntime Replication;

        double Now = 0.0;

        PublishRig()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
            Layout.Seal();

            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<LocalTransform>(entity, LocalTransform{});
            Entities.AddComponent<NetReplicated>(entity, NetReplicated{});

            EXPECT_TRUE(Host.Host(0, SampleIdentity()));
            EXPECT_TRUE(Client.Connect(Host.LocalAddress(), SampleIdentity()));
            for (int i = 0; i < 8; ++i)
                Pump();
            EXPECT_TRUE(Client.IsConnected());
        }

        void Pump()
        {
            Now += 1.0 / 60.0;
            (void)Host.Pump(Now);
            (void)Client.Pump(Now);
            Host.Flush(Now);
            Client.Flush(Now);
        }

        // One frame that advanced the simulation by `ticks`, publishing the way
        // the frame phase does: once per frame, stamped with the tick index.
        [[nodiscard]] std::uint32_t Frame(std::uint64_t tick)
        {
            const ReplicationRuntime::PublishStats stats =
                Replication.Publish(Host, Entities, Layout, tick);
            Pump();
            return stats.SnapshotsSent;
        }
    };
}

TEST(ReplicationCadence, TheDefaultIsSlowerThanTheSimulation)
{
    // Not a preference: publishing every tick is what puts the per-peer bill
    // out of reach at player counts a deathmatch needs.
    PublishRig rig;
    EXPECT_GT(rig.Replication.PublishIntervalTicks(), 1u);

    std::uint32_t sent = 0;
    for (std::uint64_t tick = 1; tick <= 60; ++tick)
        sent += rig.Frame(tick);

    EXPECT_EQ(sent, 60u / rig.Replication.PublishIntervalTicks());
}

// A machine rendering faster than it simulates runs frames that advanced no
// tick at all. Publishing on those spends bandwidth restating a moment the
// world has not moved past.
TEST(ReplicationCadence, AFrameThatSimulatedNothingSendsNothing)
{
    PublishRig rig;
    rig.Replication.SetPublishInterval(1);

    EXPECT_EQ(rig.Frame(10), 1u);
    // Three frames at the same tick: the renderer is ahead of the simulation.
    EXPECT_EQ(rig.Frame(10), 0u);
    EXPECT_EQ(rig.Frame(10), 0u);
    EXPECT_EQ(rig.Frame(11), 1u);
}

// The pacing has to be a difference, not a remainder. A host running below its
// tick rate advances the tick index by several per frame and lands on a stride
// only by luck; a remainder test would silence it for the rest of the session.
TEST(ReplicationCadence, AHostRunningBelowTickRateStillPublishes)
{
    PublishRig rig;
    rig.Replication.SetPublishInterval(2);

    // Each frame catches up three ticks -- 20fps against a 60Hz simulation.
    // Every tick index is odd or even in a fixed pattern that never hits a
    // multiple of two on the odd frames.
    std::uint32_t sent = 0;
    for (std::uint64_t frame = 1; frame <= 20; ++frame)
        sent += rig.Frame(frame * 3);

    // Every frame is at least two ticks past the last publish, so every frame
    // publishes. A remainder gate would send on half of them at best.
    EXPECT_EQ(sent, 20u);
}

TEST(ReplicationCadence, ASlowerCadenceStillPublishesEveryIntervalExactly)
{
    PublishRig rig;
    rig.Replication.SetPublishInterval(4);

    std::vector<std::uint64_t> publishedAt;
    for (std::uint64_t tick = 1; tick <= 20; ++tick)
    {
        if (rig.Frame(tick) > 0)
            publishedAt.push_back(tick);
    }

    const std::vector<std::uint64_t> expected{ 1, 5, 9, 13, 17 };
    EXPECT_EQ(publishedAt, expected);
}

// A client draws mirrored entities behind the newest sample that can
// physically have arrived. That distance is flight time plus the interval
// between snapshots -- so a client told nothing about the cadence presents
// ahead of every sample it has and holds the pose instead of blending.
TEST(ReplicationCadence, PresentationLagAccountsForTheIntervalAndTheMargin)
{
    ReplicationInterpolation interpolation;
    interpolation.SetDelayTicks(2);

    interpolation.SetSnapshotInterval(1);
    EXPECT_EQ(interpolation.PresentationLagTicks(), 3u);

    // Three ticks between snapshots means the newest one is already up to three
    // ticks old before the network is involved.
    interpolation.SetSnapshotInterval(3);
    EXPECT_EQ(interpolation.PresentationLagTicks(), 5u);

    // Zero is not a cadence.
    interpolation.SetSnapshotInterval(0);
    EXPECT_EQ(interpolation.SnapshotIntervalTicks(), 1u);
}

//=============================================================================
// The entity cap
//
// A snapshot names at most a fixed number of entities. Which ones it drops is
// a wire decision, so it has to be the same on two machines running the same
// simulation -- and dropping one must not be mistaken for the entity dying.
//=============================================================================

TEST(ReplicationCadence, TheEntityCapDropsTheSameEntitiesEveryRun)
{
    const std::size_t cap = ReplicationDefaultCaps().MaxEntitiesPerSnapshot;

    WorldComponentSchema schema;
    ReplicationLayout layout;
    ComponentRegistrar components(&schema, nullptr, &layout);
    RegisterEngineComponents(components);
    schema.Seal();
    layout.Seal();

    World world;
    schema.Apply(world);

    // More entities than one snapshot can name, spread across archetypes so
    // storage order and identity order genuinely disagree: half carry an extra
    // component, which puts them in a different chunk.
    ReplicationAuthorityIdentity identity;
    for (std::size_t i = 0; i < cap + 64; ++i)
    {
        const EntityId entity = world.CreateEntity();
        world.AddComponent<LocalTransform>(entity, LocalTransform{});
        world.AddComponent<NetReplicated>(entity, NetReplicated{});
        if (i % 2 == 0)
            world.AddComponent<LookOrientation>(entity, LookOrientation{});
        (void)identity.IdFor(entity);
    }

    // A buffer far larger than a datagram: the cap is what is being measured,
    // not the byte budget.
    std::vector<std::byte> scratch(1u << 20);
    ReplicationPeerState peer;
    SnapshotWriteRequest request;
    ReplicationChangeStore changes;
    changes.Update(world, layout, identity, 1);
    request.Changes = &changes;
    request.Layout = &layout;
    request.Peer = &peer;
    request.OwnerPeer = 1;
    request.Tick = 1;
    request.Sequence = peer.NextSnapshotSequence();

    const SnapshotWriteResult written =
        ReplicationWriteSnapshot(request, std::span(scratch));
    ASSERT_TRUE(written.Ok);
    EXPECT_EQ(written.EntitiesWritten, cap);

    // The survivors are the lowest identities, which is a property of the
    // entity's name rather than of where its chunk happened to sit. Confirming
    // the snapshot is what turns what was sent into what the peer holds.
    NetSnapshotAck applied;
    applied.Observe(request.Sequence);
    peer.Acknowledge(applied);
    ASSERT_EQ(peer.Size(), cap);
    std::uint64_t highest = 0;
    for (const auto& [id, record] : peer.All())
        highest = std::max(highest, id.Value);
    EXPECT_EQ(highest, static_cast<std::uint64_t>(cap));
}

// An entity there was no room for is still alive. Telling a peer to destroy it
// would be a lie the next snapshot has to take back by respawning it -- and on
// the client that is a destroyed-and-recreated entity, which loses everything
// not carried in a spawn.
TEST(ReplicationCadence, AnEntityTheCapDroppedIsNotReportedDead)
{
    const std::size_t cap = ReplicationDefaultCaps().MaxEntitiesPerSnapshot;

    WorldComponentSchema schema;
    ReplicationLayout layout;
    ComponentRegistrar components(&schema, nullptr, &layout);
    RegisterEngineComponents(components);
    schema.Seal();
    layout.Seal();

    World world;
    schema.Apply(world);
    ReplicationAuthorityIdentity identity;

    // One entity the peer already knows about, whose identity sorts last so the
    // cap is guaranteed to drop it.
    std::vector<EntityId> entities;
    for (std::size_t i = 0; i < cap + 1; ++i)
    {
        const EntityId entity = world.CreateEntity();
        world.AddComponent<LocalTransform>(entity, LocalTransform{});
        world.AddComponent<NetReplicated>(entity, NetReplicated{});
        entities.push_back(entity);
        (void)identity.IdFor(entity);
    }
    const NetEntityId dropped = identity.TryFind(entities.back());
    ASSERT_TRUE(dropped.IsValid());

    // Seed the peer as already holding it, so a destroy record would be
    // produced for it if the cap were mistaken for death.
    ReplicationPeerState peer;
    const std::uint32_t seeded = peer.NextSnapshotSequence();
    peer.BeginSnapshot(seeded);
    peer.RecordSent(seeded, dropped, 1);
    NetSnapshotAck seededAck;
    seededAck.Observe(seeded);
    peer.Acknowledge(seededAck);
    ASSERT_TRUE(peer.Knows(dropped));

    std::vector<std::byte> scratch(1u << 20);
    SnapshotWriteRequest request;
    ReplicationChangeStore changes;
    changes.Update(world, layout, identity, 1);
    request.Changes = &changes;
    request.Layout = &layout;
    request.Peer = &peer;
    request.OwnerPeer = 1;
    request.Tick = 1;
    request.Sequence = peer.NextSnapshotSequence();

    const SnapshotWriteResult written =
        ReplicationWriteSnapshot(request, std::span(scratch));
    ASSERT_TRUE(written.Ok);
    EXPECT_EQ(written.EntitiesDestroyed, 0u)
        << "an entity the cap had no room for was reported destroyed";
}

//=============================================================================
// How much the authority says at once.
//
// The sibling of the cadence: how often against how much. Both trade a peer's
// convergence on the world against the share of the link the authority takes to
// get it there, and neither loses anything by being turned down -- what does not
// fit rides a later snapshot.
//=============================================================================

TEST(ReplicationBudgetRuntime, TheBudgetIsClampedToWhatOneDatagramCanCarry)
{
    ReplicationRuntime runtime;
    EXPECT_EQ(runtime.SnapshotBytes(), kNetMaxSnapshotBytes)
        << "the shipping default is not what a datagram holds";

    runtime.SetSnapshotBytes(kNetMaxSnapshotBytes * 4);
    EXPECT_EQ(runtime.SnapshotBytes(), kNetMaxSnapshotBytes)
        << "a snapshot larger than a datagram is one that never arrives: there "
           "is no fragmentation behind this channel";

    runtime.SetSnapshotBytes(1);
    EXPECT_EQ(runtime.SnapshotBytes(), kNetMinSnapshotBytes)
        << "a budget below the floor is envelope and nothing else";

    runtime.SetSnapshotBytes(512);
    EXPECT_EQ(runtime.SnapshotBytes(), 512u);
}

// The budget is what the writer is given, not a number kept beside it. A peer
// served under a lowered budget receives smaller datagrams, and receives the
// rest of the world in the ones after.
TEST(ReplicationBudgetRuntime, LoweringTheBudgetLowersWhatGoesOut)
{
    const auto publishOnce = [](std::size_t budget) {
        PublishRig rig;
        rig.Replication.SetPublishInterval(1);
        rig.Replication.SetSnapshotBytes(budget);
        // A world well past what the narrow budget can carry at once.
        for (std::size_t i = 0; i < 60; ++i)
        {
            const EntityId entity = rig.Entities.CreateEntity();
            rig.Entities.AddComponent<LocalTransform>(entity, LocalTransform{});
            rig.Entities.AddComponent<NetReplicated>(entity, NetReplicated{});
        }

        // Two publishes, and the second is the one measured: on a peer's very
        // first snapshot nothing has been waiting yet, so the age of the oldest
        // deferred entity is only meaningful once there is a snapshot it could
        // have ridden and did not.
        (void)rig.Replication.Publish(rig.Host, rig.Entities, rig.Layout, 1);
        const ReplicationRuntime::PublishStats stats =
            rig.Replication.Publish(rig.Host, rig.Entities, rig.Layout, 2);
        EXPECT_EQ(stats.SnapshotsSent, 1u);
        EXPECT_EQ(stats.BudgetBytes, budget);
        // Held as well as returned, so a panel reads the same publish the frame
        // just made rather than one of its own.
        EXPECT_EQ(rig.Replication.LastPublish().BudgetBytes, budget);
        return stats;
    };

    const ReplicationRuntime::PublishStats wide = publishOnce(kNetMaxSnapshotBytes);
    const ReplicationRuntime::PublishStats narrow = publishOnce(256);

    EXPECT_LE(narrow.PeakSnapshotBytes, 256u);
    EXPECT_GT(wide.PeakSnapshotBytes, narrow.PeakSnapshotBytes);
    // The world did not shrink, so what the narrow one could not carry is
    // waiting rather than gone.
    EXPECT_GT(narrow.EntitiesDeferred, wide.EntitiesDeferred);
    EXPECT_GT(narrow.OldestDeferredSnapshots, 0u)
        << "entities were deferred but nothing reported how long they have "
           "been waiting";
}
