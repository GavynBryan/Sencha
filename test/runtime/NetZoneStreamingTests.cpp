#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/LoopbackTransport.h>
#include <net/NetOwnership.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>
#include <net/NetZoneScope.h>
#include <net/NetZoneStreaming.h>
#include <net/ReplicationRuntime.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include "StreamingTraversalFixture.h"

#include <algorithm>
#include <vector>

//=============================================================================
// Streaming and the session, kept in agreement
//
// One call a frame does all of it, so these are written against that call
// rather than against its parts. What a game got wrong here was never the
// folding, it was the order -- and the order is no longer a game's to get
// wrong.
//
// Run over the real streaming path -- demand, async load, residency processing
// -- on an eight-room chain, because "the focus was set" is not the claim. The
// claim is that the ground under a player is loaded.
//=============================================================================

namespace
{
    using StreamingTraversal::Harness;
    using StreamingTraversal::ZoneAt;
    using StreamingTraversal::kZoneCount;
    using StreamingTraversal::kZoneSpan;

    Vec3d InZone(int index)
    {
        return Vec3d{ static_cast<float>(index * kZoneSpan + kZoneSpan * 0.5),
                      1.0f, 0.0f };
    }

    NetIdentity StreamingIdentity()
    {
        return NetIdentity{ .ModuleFingerprint = 0x57ea,
                            .ReplicationTableHash = 0x21e,
                            .WorldIdentity = 0,
                            .FixedTickRateMilliHz = 60000 };
    }

    class NetZoneStreamingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(Rig.LoadManifest().empty());
            ASSERT_TRUE(Host.Host(0, StreamingIdentity()));
            ASSERT_TRUE(Client.Connect(Host.LocalAddress(), StreamingIdentity()));
            Pump(12);
            ASSERT_TRUE(Client.IsConnected());
        }

        void Pump(int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                Now += 1.0 / 60.0;
                (void)Host.Pump(Now);
                (void)Client.Pump(Now);
                Host.Flush(Now);
                Client.Flush(Now);
            }
        }

        EntityId Pawn(PeerId peer, int zoneIndex)
        {
            World& world = Rig.World().Entities();
            const EntityId entity = world.CreateEntity();
            Transform3f placed;
            placed.Position = InZone(zoneIndex);
            world.AddComponent<WorldTransform>(entity, WorldTransform{ placed });
            NetSetOwner(world, entity, peer);
            return entity;
        }

        // A frame the way the engine runs it: streaming first, then the
        // partition update that consumes it.
        void Step(NetSession* session, int frames = 4)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                Streaming.Update(Rig.World().Entities(), session, Replication,
                                 Rig.Partition(), nullptr);
                Rig.StepFrame();
            }
            Rig.SettleLoads();
        }

        void StepAsHost(int frames = 4) { Step(&Host, frames); }

        [[nodiscard]] bool Resident(int zoneIndex)
        {
            return Rig.World().FindZone(ZoneAt(zoneIndex)) != nullptr;
        }

        [[nodiscard]] std::vector<ZoneId> InterestOf(PeerId peer)
        {
            for (const NetPeerZoneInterest& record : Streaming.Interest())
            {
                if (record.Peer == peer)
                    return { record.Zones.begin(), record.Zones.end() };
            }
            return {};
        }

        [[nodiscard]] static bool Holds(const std::vector<ZoneId>& zones, int index)
        {
            return std::find(zones.begin(), zones.end(), ZoneAt(index))
                   != zones.end();
        }

        PeerId FirstPeer() const { return Host.ConnectedPeers()[0]; }

        Harness Rig{ 0, kZoneCount };
        NetZoneStreaming Streaming;
        ReplicationRuntime Replication;
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport ClientTransport{ Network };
        NetSession Host{ HostTransport };
        NetSession Client{ ClientTransport };
        double Now = 0.0;
    };
}

// Outside a session this is just streaming around the player, which is what a
// single-player game gets for free by handing its partition over.
TEST_F(NetZoneStreamingTest, WithNoSessionTheWorldFollowsTheLocalPlayer)
{
    World& world = Rig.World().Entities();
    const EntityId pawn = world.CreateEntity();
    Transform3f placed;
    placed.Position = InZone(3);
    world.AddComponent<WorldTransform>(pawn, WorldTransform{ placed });
    NetSetLocalControl(world, pawn, nullptr);

    Step(nullptr);

    EXPECT_TRUE(Resident(3)) << "the room the player is standing in is not loaded";
}

// Hosting, the world is loaded around every player at once.
TEST_F(NetZoneStreamingTest, AHostKeepsTheGroundUnderEveryPeer)
{
    Pawn(FirstPeer(), 0);
    Pawn(PeerId{ 9 }, 7);

    StepAsHost();

    EXPECT_TRUE(Resident(0));
    EXPECT_TRUE(Resident(7));
    EXPECT_EQ(Streaming.PeerSources().size(), 2u);
}

TEST_F(NetZoneStreamingTest, EveryPeersOwnRoomIsSimulated)
{
    Pawn(FirstPeer(), 0);
    Pawn(PeerId{ 9 }, 7);

    StepAsHost();

    for (const int zone : { 0, 7 })
    {
        const RuntimeZoneRecord* record = Rig.World().FindZone(ZoneAt(zone));
        ASSERT_NE(record, nullptr) << "zone " << zone;
        EXPECT_TRUE(record->Participation.Logic)
            << "zone " << zone << " has a player in it and is not simulated";
    }
}

// Why a source is keyed by entity and not by peer: somebody driving a turret
// still has a body sitting in it, and the room under that body has to stay
// simulated for them to get back into it.
TEST_F(NetZoneStreamingTest, OnePeerDrivingTwoThingsHoldsBothRooms)
{
    Pawn(FirstPeer(), 0);
    Pawn(FirstPeer(), 7);

    StepAsHost();

    EXPECT_TRUE(Resident(0)) << "the body this player left behind fell out of the world";
    EXPECT_TRUE(Resident(7));
    EXPECT_EQ(Streaming.PeerSources().size(), 2u);
}

// NetOwner replicates, so a client walking it would load the ground under every
// other player. Silent waste rather than a visible failure, which is why the
// role decides rather than the caller.
TEST_F(NetZoneStreamingTest, AClientDoesNotStreamAroundOtherPlayers)
{
    Pawn(FirstPeer(), 0);
    Pawn(PeerId{ 9 }, 7);

    Step(&Client);

    EXPECT_TRUE(Streaming.PeerSources().empty());
    EXPECT_FALSE(Resident(0));
    EXPECT_FALSE(Resident(7));
}

TEST_F(NetZoneStreamingTest, APeerThatStopsDrivingStopsHoldingItsRoom)
{
    const EntityId first = Pawn(FirstPeer(), 0);
    Pawn(PeerId{ 9 }, 7);
    StepAsHost();
    ASSERT_TRUE(Resident(0));

    NetClearOwner(Rig.World().Entities(), first);
    StepAsHost();

    EXPECT_FALSE(Resident(0)) << "nothing released the room the departed peer held";
    EXPECT_TRUE(Resident(7)) << "the peer still playing lost its own room";
}

TEST_F(NetZoneStreamingTest, ADestroyedPawnReleasesItsRoom)
{
    const EntityId first = Pawn(FirstPeer(), 0);
    Pawn(PeerId{ 9 }, 7);
    StepAsHost();
    ASSERT_TRUE(Resident(0));

    Rig.World().Entities().DestroyEntity(first);
    StepAsHost();

    EXPECT_FALSE(Resident(0));
    EXPECT_EQ(Streaming.PeerSources().size(), 1u);
}

TEST_F(NetZoneStreamingTest, APeerWalkingTheChainStreamsAheadOfItself)
{
    const EntityId pawn = Pawn(FirstPeer(), 0);
    StepAsHost();
    ASSERT_TRUE(Resident(0));

    World& world = Rig.World().Entities();
    for (int zone = 1; zone <= 3; ++zone)
    {
        world.TryGet<WorldTransform>(pawn)->Value.Position = InZone(zone);
        StepAsHost();
    }

    EXPECT_TRUE(Resident(3)) << "the peer walked out of its own residency";
    EXPECT_FALSE(Resident(0)) << "nothing behind the peer was ever released";
    EXPECT_EQ(Streaming.PeerSources().size(), 1u) << "walking minted a second source";
}

//-----------------------------------------------------------------------------
// What each peer is offered
//-----------------------------------------------------------------------------

TEST_F(NetZoneStreamingTest, APeerIsOfferedItsOwnNeighborhood)
{
    Pawn(FirstPeer(), 3);
    StepAsHost();

    const std::vector<ZoneId> mine = InterestOf(FirstPeer());
    EXPECT_TRUE(Holds(mine, 3)) << "a peer was not offered the room it is in";
    EXPECT_TRUE(Holds(mine, 2)) << "the room next door was not offered ahead of time";
    EXPECT_TRUE(Holds(mine, 4));
    EXPECT_FALSE(Holds(mine, 0)) << "a peer was offered a room nobody is near";
}

TEST_F(NetZoneStreamingTest, APeerIsNotOfferedAnotherPlayersRooms)
{
    Pawn(FirstPeer(), 0);
    Pawn(PeerId{ 9 }, 7);
    StepAsHost();

    EXPECT_TRUE(Holds(InterestOf(FirstPeer()), 0));
    EXPECT_FALSE(Holds(InterestOf(FirstPeer()), 7))
        << "one peer was offered the other's room";
}

// Placed two rooms apart so the neighbourhoods genuinely overlap: further apart
// and there is nothing to collapse, so a build that never deduplicated passes.
TEST_F(NetZoneStreamingTest, APeerDrivingTwoThingsIsOfferedBothNeighborhoods)
{
    Pawn(FirstPeer(), 2);
    Pawn(FirstPeer(), 4);
    StepAsHost();

    ASSERT_EQ(Streaming.Interest().size(), 1u) << "one peer, one interest set";
    const std::vector<ZoneId> mine = InterestOf(FirstPeer());
    EXPECT_TRUE(Holds(mine, 2));
    EXPECT_TRUE(Holds(mine, 4));
    EXPECT_TRUE(Holds(mine, 3)) << "the room between them was offered by neither";
    EXPECT_TRUE(std::is_sorted(mine.begin(), mine.end(),
                               [](ZoneId a, ZoneId b) { return a.Value < b.Value; }));
    EXPECT_EQ(std::adjacent_find(mine.begin(), mine.end()), mine.end())
        << "a room both of them are near was offered twice";
}

TEST_F(NetZoneStreamingTest, TheGrantsAPeerNeedsActuallyReachIt)
{
    Pawn(FirstPeer(), 3);
    StepAsHost();
    Pump(4);

    const ReplicationPeerState* baseline = Replication.PeerBaseline(FirstPeer());
    ASSERT_NE(baseline, nullptr);
    EXPECT_GT(baseline->Zones().Size(), 0u)
        << "streaming ran and no room was ever offered to the peer";
}

//-----------------------------------------------------------------------------
// The client's half
//-----------------------------------------------------------------------------

// The deadlock this prevents: a player somewhere their own policy has no reason
// to want. The authority withholds the room until it is acked, and the ack
// waits on a load nothing asked for.
TEST_F(NetZoneStreamingTest, AGrantedRoomIsLoadedWithNoLocalReasonToWantIt)
{
    // Standing at one end, granted a room at the other.
    Rig.Partition().SetFocus(InZone(0));
    Replication.ApplyZoneScope(NetZoneScopeUpdate{
        .Zone = ZoneAt(5), .Verb = NetZoneScopeVerb::Grant });

    Step(&Client);

    EXPECT_TRUE(Resident(5)) << "a granted room was never loaded, so it can never "
                                "be acked, so it is never filled";
    EXPECT_EQ(Streaming.PinnedGrants().size(), 1u);
}

TEST_F(NetZoneStreamingTest, ARevokedRoomIsLetGoAgain)
{
    Rig.Partition().SetFocus(InZone(0));
    Replication.ApplyZoneScope(NetZoneScopeUpdate{
        .Zone = ZoneAt(5), .Verb = NetZoneScopeVerb::Grant });
    Step(&Client);
    ASSERT_TRUE(Resident(5));

    Replication.ApplyZoneScope(NetZoneScopeUpdate{
        .Zone = ZoneAt(5), .Verb = NetZoneScopeVerb::Revoke });
    Step(&Client);

    EXPECT_TRUE(Streaming.PinnedGrants().empty());
    EXPECT_FALSE(Resident(5)) << "a client holds every room it was ever granted";
}
