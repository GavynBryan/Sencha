#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/LoopbackTransport.h>
#include <net/NetZoneStreaming.h>
#include <net/NetOwnership.h>
#include <net/NetSession.h>
#include <net/NetZoneScope.h>
#include <net/ReplicationRuntime.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include "StreamingTraversalFixture.h"

#include <algorithm>
#include <vector>

//=============================================================================
// Adding a relevance rule
//
// G3's contributor gate, asked as a test rather than asserted in a document:
// once scoped replication exists, can somebody add another relevance policy --
// always relevant, same-zone, within distance -- without editing packet
// protocol machinery?
//
// The claim these hold the tree to is that the surface is a list of zones per
// peer, and a policy is whatever produces that list. Nothing below is allowed
// to reach into net/: each case composes an interest set from outside and the
// grants follow. If that stops being possible, these fail, and the reason will
// be that a policy has been baked into the mechanism.
//=============================================================================

namespace
{
    using StreamingTraversal::Harness;
    using StreamingTraversal::ZoneAt;
    using StreamingTraversal::kZoneSpan;

    NetIdentity RelevanceIdentity()
    {
        return NetIdentity{
            .ModuleFingerprint = 0x12345,
            .ReplicationTableHash = 0x67890,
            .WorldIdentity = 0,
            .FixedTickRateMilliHz = 60000,
        };
    }

    Vec3d Inside(int index)
    {
        return Vec3d{ static_cast<float>(index * kZoneSpan + kZoneSpan * 0.5),
                      1.0f,
                      0.0f };
    }

    // A streaming authority with two connected peers, driven by hand. The pawns
    // are the authority's own entities: what a peer is offered follows from
    // where the thing it drives is, which is the only input a policy has.
    class RelevancePolicyTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const std::string error = Rig.LoadManifest();
            ASSERT_TRUE(error.empty()) << error;

            ASSERT_TRUE(Host.Host(0, RelevanceIdentity()));
            ASSERT_TRUE(First.Connect(Host.LocalAddress(), RelevanceIdentity()));
            ASSERT_TRUE(Second.Connect(Host.LocalAddress(), RelevanceIdentity()));
            Pump(12);
            ASSERT_EQ(Host.ConnectedPeers().size(), 2u);

            StreamedZones.clear();
            for (const ZoneHeader& zone : Rig.Partition().Manifest().Zones)
                StreamedZones.push_back(zone.Id);
            Replication.SetStreamedZones(StreamedZones);
        }

        void Pump(int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                Now += 1.0 / 60.0;
                (void)Host.Pump(Now);
                (void)First.Pump(Now);
                (void)Second.Pump(Now);
                Host.Flush(Now);
                First.Flush(Now);
                Second.Flush(Now);
            }
        }

        EntityId Pawn(PeerId peer, int zoneIndex)
        {
            World& world = Rig.World().Entities();
            const EntityId entity = world.CreateEntity();
            Transform3f placed;
            placed.Position = Inside(zoneIndex);
            world.AddComponent<WorldTransform>(entity, WorldTransform{ placed });
            NetSetOwner(world, entity, peer);
            return entity;
        }

        // The default policy: what each peer drives, and its neighbourhood.
        void RunStreaming(int frames = 4)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                // Streaming only; each case publishes its own interest set
                // below, which is the point of the surface.
                Focus.Update(Rig.World().Entities(), EntityId{}, &Host, Replication,
                             Rig.Partition(), nullptr);
                Rig.StepFrame();
            }
            Rig.SettleLoads();
        }

        // What one peer ended up holding open, ascending.
        [[nodiscard]] std::vector<ZoneId> ScopeOf(PeerId peer)
        {
            std::vector<ZoneId> zones;
            const ReplicationPeerState* baseline = Replication.PeerBaseline(peer);
            if (baseline == nullptr)
                return zones;
            for (const NetZoneScope::Entry& held : baseline->Zones().Entries())
                zones.push_back(held.Zone);
            return zones;
        }

        [[nodiscard]] static bool Holds(const std::vector<ZoneId>& zones, int index)
        {
            return std::find(zones.begin(), zones.end(), ZoneAt(index))
                   != zones.end();
        }

        PeerId FirstPeer() const { return Host.ConnectedPeers()[0]; }
        PeerId SecondPeer() const { return Host.ConnectedPeers()[1]; }

        Harness Rig;
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport FirstTransport{ Network };
        LoopbackTransport SecondTransport{ Network };
        NetSession Host{ HostTransport };
        NetSession First{ FirstTransport };
        NetSession Second{ SecondTransport };
        ReplicationRuntime Replication;
        NetZoneStreaming Focus;
        std::vector<ZoneId> StreamedZones;
        double Now = 0.0;
    };
}

// The baseline the rest are measured against: no policy at all beyond the one
// the engine ships, and each peer holds its own neighbourhood.
TEST_F(RelevancePolicyTest, WithNoPolicyEachPeerHoldsItsOwnNeighborhood)
{
    Pawn(FirstPeer(), 0);
    Pawn(SecondPeer(), 6);
    RunStreaming();

    (void)Replication.PublishZoneScope(Host, Focus.Interest());

    EXPECT_TRUE(Holds(ScopeOf(FirstPeer()), 0));
    EXPECT_FALSE(Holds(ScopeOf(FirstPeer()), 6));
    EXPECT_TRUE(Holds(ScopeOf(SecondPeer()), 6));
    EXPECT_FALSE(Holds(ScopeOf(SecondPeer()), 0));
}

// Policy one: always relevant. A hub, a boss arena, a room a scripted event is
// running in -- somewhere every player must be told about whether or not
// anybody is near it.
//
// Written entirely here. Nothing in net/ knows the words "always relevant".
TEST_F(RelevancePolicyTest, AlwaysRelevantZonesReachEveryPeer)
{
    Pawn(FirstPeer(), 0);
    Pawn(SecondPeer(), 6);
    RunStreaming();

    const ZoneId hub = ZoneAt(3);

    // The policy: whatever each peer already wants, plus the hub.
    std::vector<std::vector<ZoneId>> zones;
    std::vector<NetPeerZoneInterest> interest;
    for (const NetPeerZoneInterest& record : Focus.Interest())
    {
        std::vector<ZoneId> mine(record.Zones.begin(), record.Zones.end());
        if (!std::binary_search(mine.begin(), mine.end(), hub,
                                [](ZoneId a, ZoneId b) { return a.Value < b.Value; }))
        {
            mine.insert(std::lower_bound(mine.begin(), mine.end(), hub,
                                         [](ZoneId a, ZoneId b)
                                         { return a.Value < b.Value; }),
                        hub);
        }
        zones.push_back(std::move(mine));
    }
    std::size_t at = 0;
    for (const NetPeerZoneInterest& record : Focus.Interest())
        interest.push_back({ .Peer = record.Peer, .Zones = zones[at++] });

    (void)Replication.PublishZoneScope(Host, interest);

    EXPECT_TRUE(Holds(ScopeOf(FirstPeer()), 3))
        << "an always-relevant room did not reach a peer nowhere near it";
    EXPECT_TRUE(Holds(ScopeOf(SecondPeer()), 3));
    // And it is additive: each peer still has its own.
    EXPECT_TRUE(Holds(ScopeOf(FirstPeer()), 0));
    EXPECT_TRUE(Holds(ScopeOf(SecondPeer()), 6));
}

// Policy two: same-zone only. A game that does not want neighbours replicated
// -- a room-by-room horror game, an arena shooter with no sightlines between
// rooms -- narrows what a peer is offered to the room it is standing in.
//
// No new code path: it is the room each peer's own focus resolves to.
TEST_F(RelevancePolicyTest, SameZoneOnlyNarrowsWhatAPeerIsOffered)
{
    const EntityId pawn = Pawn(FirstPeer(), 3);
    RunStreaming();

    const ZoneId standingIn =
        Rig.Partition().FocusZone(NetZoneStreaming::SourceFor(pawn));
    ASSERT_TRUE(standingIn.IsValid());

    const NetPeerZoneInterest interest{ .Peer = FirstPeer(),
                                        .Zones = std::span{ &standingIn, 1 } };
    (void)Replication.PublishZoneScope(Host, std::span{ &interest, 1 });

    const std::vector<ZoneId> mine = ScopeOf(FirstPeer());
    EXPECT_EQ(mine.size(), 1u) << "a narrower policy still offered the neighbours";
    EXPECT_TRUE(Holds(mine, 3));
}

// Policy three: a peer nobody has decided about yet holds nothing, which is
// what makes "deny unless a policy says otherwise" the safe default a new
// policy is written against.
TEST_F(RelevancePolicyTest, APeerNoPolicyNamesHoldsNothing)
{
    Pawn(FirstPeer(), 0);
    Pawn(SecondPeer(), 6);
    RunStreaming();
    (void)Replication.PublishZoneScope(Host, Focus.Interest());
    ASSERT_FALSE(ScopeOf(SecondPeer()).empty());

    // A policy that has an opinion about one peer and none about the other.
    std::vector<NetPeerZoneInterest> narrowed;
    for (const NetPeerZoneInterest& record : Focus.Interest())
    {
        if (record.Peer == FirstPeer())
            narrowed.push_back(record);
    }
    (void)Replication.PublishZoneScope(Host, narrowed);

    EXPECT_FALSE(ScopeOf(FirstPeer()).empty());
    EXPECT_TRUE(ScopeOf(SecondPeer()).empty())
        << "a peer the policy said nothing about kept what it had";
}

// The gate itself, stated as an assertion about the diff rather than about
// behaviour: every case above changed which zones went into a list. None of
// them touched a payload kind, an encoder, a channel, a sequence number, or the
// snapshot writer. A policy is a list of zones per peer, and that is the whole
// surface.
TEST_F(RelevancePolicyTest, EveryPolicyAboveIsAListOfZonesAndNothingElse)
{
    Pawn(FirstPeer(), 2);
    RunStreaming();

    // Three different policies over one unchanged mechanism, in one test, to
    // make the point concrete: the only thing that varies is the span.
    const ZoneId one = ZoneAt(2);
    const ZoneId two[] = { ZoneAt(2), ZoneAt(5) };

    const NetPeerZoneInterest narrow{ .Peer = FirstPeer(),
                                      .Zones = std::span{ &one, 1 } };
    (void)Replication.PublishZoneScope(Host, std::span{ &narrow, 1 });
    EXPECT_EQ(ScopeOf(FirstPeer()).size(), 1u);

    const NetPeerZoneInterest wide{ .Peer = FirstPeer(), .Zones = two };
    (void)Replication.PublishZoneScope(Host, std::span{ &wide, 1 });
    EXPECT_EQ(ScopeOf(FirstPeer()).size(), 2u);

    (void)Replication.PublishZoneScope(Host, {});
    EXPECT_TRUE(ScopeOf(FirstPeer()).empty());
}
