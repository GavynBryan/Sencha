#include <gtest/gtest.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/LoopbackTransport.h>
#include <net/NetPlayerCommand.h>
#include <net/NetProtocol.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>
#include <net/PawnCommandRing.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationRuntime.h>
#include <net/SimulatedTransport.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cstdint>
#include <vector>

//=============================================================================
// A full session at the envelope this work exists to reach.
//
// Every mechanism below it has its own test against inputs handed to it: the
// cadence gate, the change store, the fill order, the budget, the quantizer.
// What none of them can say is what a minute of them running into each other
// costs, or whether sixteen peers all being served at once still converges --
// which is the only question the envelope is actually about.
//
// So this runs the real path: one authority, sixteen real sessions with real
// handshakes over links that lose and delay, replication publishing at its
// shipping cadence into its shipping budget, and clients acknowledging what they
// applied through the command channel the way production does. Seeded and
// stepped by hand, so a failure replays from its seed rather than from having
// been unlucky.
//
// What it is not: no physics, no prediction, no input. Those have their own
// convergence suite. This is about whether the authority can describe a world to
// sixteen machines inside a datagram each, repeatedly, without any of them
// falling behind and staying there.
//
// Because these clients send no input they also send no acknowledgements -- a
// command message cannot presently carry an acknowledgement without carrying at
// least one input record -- so every entity here rides as full state on every
// snapshot, at roughly twice what a delta costs. That makes everything below a
// lower bound rather than a projection: sixteen peers converge here without the
// delta machinery helping them at all. It also costs a real client bandwidth
// during the one window where it has the least to spare, which is recorded in
// networking.md rather than fixed here: making an acknowledgement travel on its
// own is a change to the command format.
//=============================================================================

namespace
{
    constexpr double kTickSeconds = 1.0 / 60.0;
    constexpr std::size_t kPeers = 16;
    // Entities beyond the players: projectiles, pickups, the flag. More than one
    // datagram of full state can hold, which is the condition the fill exists
    // for.
    constexpr std::size_t kSceneEntities = 80;

    NetIdentity SoakIdentity()
    {
        NetIdentity identity;
        identity.ModuleFingerprint = 0x50AC;
        identity.WorldIdentity = 0x5CE7;
        identity.FixedTickRateMilliHz = 60000;
        return identity;
    }

    // Deterministic and cheap. Anything seeded from a clock would make a failure
    // here unreproducible, which is the one thing a soak must not be.
    struct Rolls
    {
        std::uint32_t State = 0x13579BDFu;
        std::uint32_t Next()
        {
            State = State * 1664525u + 1013904223u;
            return State >> 8;
        }
        float Unit() { return static_cast<float>(Next() % 10000u) / 10000.0f; }
    };

    struct Endpoint
    {
        Endpoint(LoopbackNetwork& network, const NetImpairment& impairment)
            : Wire(network), Link(Wire, impairment), Session(Link)
        {
        }

        LoopbackTransport Wire;
        SimulatedTransport Link;
        NetSession Session;
    };

    // One client: its own world, its own map of what it has been told about.
    struct Peer
    {
        Peer(LoopbackNetwork& network, const NetImpairment& impairment,
             const WorldComponentSchema& schema)
            : Link(network, impairment)
        {
            schema.Apply(Entities);
        }

        Endpoint Link;
        World Entities;
        ReplicationRuntime Replication;
        PeerCommandRuntime Commands;
        PawnCommandRing Ring;
        std::uint64_t Tick = 0;
        bool Joined = false;
    };

    struct Soak
    {
        LoopbackNetwork Network;
        NetImpairment Impairment;

        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Authority;
        ReplicationRuntime Replication;
        PeerCommandRuntime Commands;
        Endpoint Host;

        std::vector<std::unique_ptr<Peer>> Peers;
        std::vector<EntityId> Scene;
        Rolls Random;
        double Now = 0.0;
        std::uint64_t Tick = 4'000'000;

        explicit Soak(const NetImpairment& impairment)
            : Impairment(impairment), Host(Network, impairment)
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Authority);
            Layout.Seal();
        }

        [[nodiscard]] bool StartHost()
        {
            // The envelope under test. A host admits four out of the box, which
            // is the co-op shape rather than the arena one.
            Host.Session.SetMaxPeers(kPeers);
            return Host.Session.Host(0, SoakIdentity());
        }

        // A world of things that move, plus one owned entity per peer.
        void BuildScene(std::size_t peers)
        {
            for (std::size_t i = 0; i < kSceneEntities; ++i)
                Scene.push_back(Spawn(0));
            for (std::size_t i = 0; i < peers; ++i)
                Scene.push_back(Spawn(static_cast<std::uint32_t>(i + 1)));
        }

        EntityId Spawn(std::uint32_t owner)
        {
            const EntityId entity = Authority.CreateEntity();
            Authority.AddComponent<NetReplicated>(entity);
            Authority.AddComponent<LocalTransform>(entity, LocalTransform{});
            if (owner != 0)
                Authority.AddComponent<NetOwner>(entity, NetOwner{ .Peer = owner });
            return entity;
        }

        Peer& AddPeer()
        {
            Peers.push_back(std::make_unique<Peer>(Network, Impairment, Schema));
            Peer& peer = *Peers.back();
            EXPECT_TRUE(peer.Link.Session.Connect(Host.Session.LocalAddress(),
                                                  SoakIdentity()));
            return peer;
        }

        // Everything drifts, which is the expensive case: nothing can be omitted
        // and the budget is contended on every publish.
        void MoveTheWorld()
        {
            for (std::size_t i = 0; i < Scene.size(); ++i)
            {
                LocalTransform* pose = Authority.TryGet<LocalTransform>(Scene[i]);
                pose->Value.Position =
                    Vec3d{ static_cast<float>(Tick % 512) + static_cast<float>(i),
                           2.0f, Random.Unit() * 8.0f };
            }
        }

        // Returns what this step actually published. A publish the cadence gate
        // skipped has nothing to report, and reading the runtime's held stats
        // instead would count the previous publish again on every skipped tick.
        ReplicationRuntime::PublishStats Step()
        {
            Now += kTickSeconds;
            ++Tick;
            Host.Session.SetLocalTick(Tick);
            MoveTheWorld();

            for (const NetSession::Delivery& delivery : Host.Session.Pump(Now))
            {
                if (!delivery.Payload.empty()
                    && static_cast<NetPayloadKind>(delivery.Payload[0])
                           == NetPayloadKind::Command)
                {
                    (void)Commands.Receive(delivery.From, delivery.Payload);
                }
            }

            const ReplicationRuntime::PublishStats published =
                Replication.Publish(Host.Session, Authority, Layout, Tick,
                                    &Commands);
            Host.Session.Flush(Now);

            for (std::unique_ptr<Peer>& peer : Peers)
            {
                ++peer->Tick;
                peer->Link.Session.SetLocalTick(peer->Tick);
                for (const NetSession::Delivery& delivery :
                     peer->Link.Session.Pump(Now))
                {
                    if (delivery.Payload.empty()
                        || static_cast<NetPayloadKind>(delivery.Payload[0])
                               != NetPayloadKind::Snapshot)
                    {
                        continue;
                    }
                    (void)peer->Replication.Apply(delivery.Payload, peer->Entities,
                                                  Schema, Layout);
                }
                // What it has applied travels back on the command channel, which
                // is the only thing that lets the authority stop describing what
                // it already holds.
                (void)peer->Commands.SendLocal(peer->Link.Session, peer->Ring,
                                               peer->Replication.AppliedAck());
                peer->Link.Session.Flush(Now);
                peer->Joined = peer->Joined || peer->Link.Session.IsConnected();
            }
            return published;
        }

        // How many of the authority's entities this peer holds.
        [[nodiscard]] std::size_t Seen(const Peer& peer) const
        {
            return peer.Replication.ClientEntities().Size();
        }

    };

    NetImpairment SoakLink()
    {
        NetImpairment impairment;
        impairment.LossPercent = 8;
        impairment.ReorderPercent = 4;
        impairment.LatencySteps = 3;
        impairment.JitterSteps = 2;
        impairment.Seed = 0x5EED;
        return impairment;
    }
}

// Sixteen peers, a world larger than a datagram, everything moving, for a
// minute of simulation. Every peer ends up holding the whole world.
TEST(ReplicationSoak, SixteenPeersAllConverge)
{
    Soak soak(SoakLink());
    ASSERT_TRUE(soak.StartHost());
    soak.BuildScene(kPeers);
    for (std::size_t i = 0; i < kPeers; ++i)
        (void)soak.AddPeer();

    // A minute at sixty ticks a second.
    for (int tick = 0; tick < 3600; ++tick)
        soak.Step();

    const std::size_t total = soak.Scene.size();
    for (std::size_t i = 0; i < soak.Peers.size(); ++i)
    {
        const Peer& peer = *soak.Peers[i];
        EXPECT_TRUE(peer.Joined) << "peer " << i << " never completed a handshake";
        EXPECT_EQ(soak.Seen(peer), total)
            << "peer " << i << " is still missing part of the world after a "
               "minute of being told about it";
    }
}

// A peer that arrives after the session is already saturated. This is the one
// that used to be impossible: its first snapshot is the largest it will ever be
// sent, and an authority that gave up on snapshots it could not fit never sent
// it anything at all.
TEST(ReplicationSoak, APeerJoiningASaturatedSessionIsStillSeeded)
{
    Soak soak(SoakLink());
    ASSERT_TRUE(soak.StartHost());
    soak.BuildScene(kPeers);
    for (std::size_t i = 0; i < kPeers - 1; ++i)
        (void)soak.AddPeer();

    for (int tick = 0; tick < 600; ++tick)
        soak.Step();

    // Late, into a session already spending its whole budget on everyone else.
    Peer& late = soak.AddPeer();
    for (int tick = 0; tick < 900; ++tick)
        soak.Step();

    EXPECT_TRUE(late.Joined);
    EXPECT_EQ(soak.Seen(late), soak.Scene.size())
        << "a peer that joined a busy session was never given the whole world";
}

// The bill, at the envelope. Every snapshot fits one datagram by construction --
// the writer defers rather than overflowing -- so what is worth asserting is
// that no peer is being starved to achieve it.
TEST(ReplicationSoak, NobodyIsStarvedToStayInsideTheBudget)
{
    Soak soak(SoakLink());
    ASSERT_TRUE(soak.StartHost());
    soak.BuildScene(kPeers);
    for (std::size_t i = 0; i < kPeers; ++i)
        (void)soak.AddPeer();

    std::uint32_t worstWait = 0;
    std::uint32_t unsendable = 0;
    for (int tick = 0; tick < 1800; ++tick)
    {
        const ReplicationRuntime::PublishStats stats = soak.Step();
        if (stats.SnapshotsSent == 0)
            continue;  // The cadence gate skipped this tick.
        worstWait = std::max(worstWait, stats.OldestDeferredSnapshots);
        unsendable += stats.EntitiesUnsendable;
        EXPECT_LE(stats.PeakSnapshotBytes, soak.Replication.SnapshotBytes());
    }

    EXPECT_EQ(unsendable, 0u)
        << "an entity could not be described inside the budget at all";
    // Deferral is the budget working. A queue that is not draining is not: an
    // entity waiting this many snapshots is one nobody is getting to.
    EXPECT_LT(worstWait, 64u)
        << "an entity went " << worstWait << " snapshots without being carried";
}

// Per-peer memory is the other axis the envelope moves along: sixteen peers
// costing what one peer costs sixteen times over is the shape that stopped
// scaling. What a peer keeps is a generation and a sent-sequence per entity, not
// a copy of the world.
TEST(ReplicationSoak, WhatAPeerCostsDoesNotGrowWithTime)
{
    Soak soak(SoakLink());
    ASSERT_TRUE(soak.StartHost());
    soak.BuildScene(kPeers);
    for (std::size_t i = 0; i < kPeers; ++i)
        (void)soak.AddPeer();

    for (int tick = 0; tick < 600; ++tick)
        soak.Step();
    const std::size_t settled = soak.Replication.TrackedPeers();

    for (int tick = 0; tick < 2400; ++tick)
        soak.Step();

    EXPECT_EQ(soak.Replication.TrackedPeers(), settled)
        << "the authority is tracking peers that are not connected";
    EXPECT_EQ(soak.Replication.TrackedPeers(), kPeers);
}
