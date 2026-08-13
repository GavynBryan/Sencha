#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/NetGrantedResidency.h>
#include <net/NetOwnedFocus.h>
#include <net/NetOwnership.h>
#include <net/ReplicationRuntime.h>
#include <net/NetReplicationComponents.h>
#include <physics/components/CharacterController.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include "StreamingTraversalFixture.h"

#include <algorithm>

//=============================================================================
// Residency follows the entities peers drive
//
// The zone layer merges several focus sources and never learns what any of them
// are; NetOwnedFocus is what fills that set in from a session. These run it over
// the real streaming path -- demand, async load, residency processing -- on the
// eight-zone chain, because "the source was set" is not the claim. The claim is
// that the ground under a player is loaded.
//=============================================================================

namespace
{
    using StreamingTraversal::Harness;
    using StreamingTraversal::ZoneAt;
    using StreamingTraversal::kZoneSpan;

    // Middle of zone `index`, which is where a pawn standing in it would be.
    Vec3d InZone(int index)
    {
        return Vec3d{ static_cast<float>(index * kZoneSpan + kZoneSpan * 0.5),
                      1.0f,
                      0.0f };
    }

    class NetOwnedFocusTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const std::string error = Rig.LoadManifest();
            ASSERT_TRUE(error.empty()) << error;
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

        // One frame the way the game drives it: focus from ownership first, then
        // the streaming update that consumes it.
        void Step(NetSessionRole role = NetSessionRole::Host, int frames = 4)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                Focus.Update(Rig.World().Entities(), role, Rig.Partition());
                Rig.StepFrame();
            }
            Rig.SettleLoads();
        }

        [[nodiscard]] bool Resident(int zoneIndex)
        {
            return Rig.World().FindZone(ZoneAt(zoneIndex)) != nullptr;
        }

        Harness Rig;
        NetOwnedFocus Focus;
    };
}

// The A gate: two players at opposite ends of the chain, one authority, and
// both of them standing on loaded ground.
TEST_F(NetOwnedFocusTest, AnAuthorityKeepsTheGroundUnderEveryPeer)
{
    Pawn(PeerId{ 1 }, 0);
    Pawn(PeerId{ 7 }, 7);

    Step();

    EXPECT_TRUE(Resident(0)) << "the first peer's zone is not loaded";
    EXPECT_TRUE(Resident(7)) << "the second peer's zone is not loaded";
    EXPECT_EQ(Focus.Held().size(), 2u);
    EXPECT_EQ(Rig.Partition().FocusSourceCount(), 2u);
}

// Each peer's own zone is simulated, not merely attached. A zone the authority
// holds without ticking is a player nothing happens around.
TEST_F(NetOwnedFocusTest, EveryPeersOwnZoneIsSimulated)
{
    Pawn(PeerId{ 1 }, 0);
    Pawn(PeerId{ 7 }, 7);

    Step();

    for (const int zone : { 0, 7 })
    {
        const RuntimeZoneRecord* record = Rig.World().FindZone(ZoneAt(zone));
        ASSERT_NE(record, nullptr) << "zone " << zone;
        EXPECT_TRUE(record->Participation.Logic)
            << "zone " << zone << " has a player in it and is not simulated";
    }
}

// Why the source is keyed by entity and not by peer. Somebody driving a turret
// still has a body sitting in it, and the zone under that body has to stay
// loaded for them to get back into it. Keyed per peer, one of these two zones
// would go.
TEST_F(NetOwnedFocusTest, OnePeerDrivingTwoThingsHoldsBothTheirZones)
{
    Pawn(PeerId{ 4 }, 0);
    Pawn(PeerId{ 4 }, 7);

    Step();

    EXPECT_TRUE(Resident(0)) << "the body this player left behind fell out of the world";
    EXPECT_TRUE(Resident(7));
    EXPECT_EQ(Focus.Held().size(), 2u) << "one peer, two things driven, two sources";
}

// A client holds NetOwner records for every player in the session. Walking them
// there would load the ground under everyone else as well as itself, which is a
// silent waste rather than a visible failure -- hence the role parameter.
TEST_F(NetOwnedFocusTest, AClientDoesNotStreamAroundOtherPlayers)
{
    Pawn(PeerId{ 1 }, 0);
    Pawn(PeerId{ 7 }, 7);

    Step(NetSessionRole::Client);

    EXPECT_TRUE(Focus.Held().empty());
    EXPECT_EQ(Rig.Partition().FocusSourceCount(), 0u);
    EXPECT_FALSE(Resident(0));
    EXPECT_FALSE(Resident(7));
}

// A peer handed back what it was driving stops holding the ground under it. The
// harness lingers for zero seconds, so this is the release and nothing else.
TEST_F(NetOwnedFocusTest, APeerThatStopsDrivingStopsHoldingItsZone)
{
    const EntityId first = Pawn(PeerId{ 1 }, 0);
    Pawn(PeerId{ 7 }, 7);
    Step();
    ASSERT_TRUE(Resident(0));

    NetClearOwner(Rig.World().Entities(), first);
    Step();

    EXPECT_FALSE(Resident(0)) << "nothing released the zone the departed peer held";
    EXPECT_TRUE(Resident(7)) << "the peer still playing lost its own zone";
    EXPECT_EQ(Focus.Held().size(), 1u);
}

// The same release through the other door out: the entity itself is gone.
TEST_F(NetOwnedFocusTest, ADestroyedPawnReleasesItsSource)
{
    const EntityId first = Pawn(PeerId{ 1 }, 0);
    Pawn(PeerId{ 7 }, 7);
    Step();
    ASSERT_TRUE(Resident(0));

    Rig.World().Entities().DestroyEntity(first);
    Step();

    EXPECT_FALSE(Resident(0));
    EXPECT_EQ(Focus.Held().size(), 1u);
}

// Sources move with the entity, so a peer walking the chain streams ahead of
// itself exactly as a local player does.
TEST_F(NetOwnedFocusTest, APeerWalkingTheChainStreamsAheadOfItself)
{
    const EntityId pawn = Pawn(PeerId{ 1 }, 0);
    Step();
    ASSERT_TRUE(Resident(0));

    World& world = Rig.World().Entities();
    for (int zone = 1; zone <= 3; ++zone)
    {
        world.TryGet<WorldTransform>(pawn)->Value.Position = InZone(zone);
        Step();
    }

    EXPECT_TRUE(Resident(3)) << "the peer walked out of its own residency";
    EXPECT_FALSE(Resident(0)) << "nothing behind the peer was ever released";
    EXPECT_EQ(Focus.Held().size(), 1u) << "walking minted a second source";
}

// Ids are namespaced away from the source a locally controlled pawn uses, so a
// listen server's own focus and its peers' cannot land on the same slot.
TEST(NetOwnedFocusSourceIds, PeerSourcesNeverCollideWithTheLocalOne)
{
    EXPECT_NE(NetFocusSourceFor(EntityId{ .Index = 0, .Generation = 1 }),
              kPrimaryFocusSource);
    EXPECT_NE(NetFocusSourceFor(EntityId{ .Index = 1, .Generation = 1 }),
              kPrimaryFocusSource);
    EXPECT_NE(NetFocusSourceFor(EntityId{ .Index = 3, .Generation = 1 }),
              NetFocusSourceFor(EntityId{ .Index = 4, .Generation = 1 }));

    // Slot reuse is the same slot: the source follows the position of whatever
    // is driven there now, and a generation the world has moved past has no
    // entity behind it to refresh one.
    EXPECT_EQ(NetFocusSourceFor(EntityId{ .Index = 3, .Generation = 1 }),
              NetFocusSourceFor(EntityId{ .Index = 3, .Generation = 9 }));
}

//=============================================================================
// What each peer may be told about
//
// The authority holds the union of everybody's neighborhoods; each peer is
// offered only its own. Same policy, read the other way round.
//=============================================================================

namespace
{
    // One peer's zones out of the interest set, or empty if it has none.
    std::vector<ZoneId> InterestOf(const NetOwnedFocus& focus, PeerId peer)
    {
        for (const NetPeerZoneInterest& record : focus.Interest())
        {
            if (record.Peer == peer)
                return { record.Zones.begin(), record.Zones.end() };
        }
        return {};
    }

    bool Holds(const std::vector<ZoneId>& zones, int index)
    {
        return std::find(zones.begin(), zones.end(), ZoneAt(index)) != zones.end();
    }
}

// A peer is offered the room it is in and the ones next door, which is what
// makes a crossing free: the room being entered was granted before anyone
// reached the doorway.
TEST_F(NetOwnedFocusTest, APeerIsOfferedItsOwnNeighborhood)
{
    Pawn(PeerId{ 1 }, 3);
    Step();

    const std::vector<ZoneId> mine = InterestOf(Focus, PeerId{ 1 });
    EXPECT_TRUE(Holds(mine, 3)) << "a peer was not offered the room it is standing in";
    EXPECT_TRUE(Holds(mine, 2)) << "the room next door was not offered ahead of time";
    EXPECT_TRUE(Holds(mine, 4));
    EXPECT_FALSE(Holds(mine, 0)) << "a peer was offered a room nobody is near";
}

// The whole point of per-peer scope: what one player may be told about is not
// what the authority happens to be holding.
TEST_F(NetOwnedFocusTest, APeerIsNotOfferedAnotherPlayersRooms)
{
    Pawn(PeerId{ 1 }, 0);
    Pawn(PeerId{ 7 }, 7);
    Step();

    const std::vector<ZoneId> first = InterestOf(Focus, PeerId{ 1 });
    const std::vector<ZoneId> second = InterestOf(Focus, PeerId{ 7 });

    EXPECT_TRUE(Holds(first, 0));
    EXPECT_FALSE(Holds(first, 7)) << "one peer was offered the other's room";
    EXPECT_TRUE(Holds(second, 7));
    EXPECT_FALSE(Holds(second, 0));
}

// A peer driving two things is offered both neighborhoods, collapsed into one
// set. Placed two rooms apart so the neighborhoods genuinely overlap -- far
// enough apart and there is nothing to collapse, and a build that never
// deduplicated at all would pass.
TEST_F(NetOwnedFocusTest, APeerDrivingTwoThingsIsOfferedBothNeighborhoods)
{
    Pawn(PeerId{ 4 }, 2);
    Pawn(PeerId{ 4 }, 4);
    Step();

    ASSERT_EQ(Focus.Interest().size(), 1u) << "one peer, one interest set";
    const std::vector<ZoneId> mine = InterestOf(Focus, PeerId{ 4 });
    EXPECT_TRUE(Holds(mine, 2));
    EXPECT_TRUE(Holds(mine, 4));
    EXPECT_TRUE(Holds(mine, 3)) << "the room between them was offered by neither";

    EXPECT_TRUE(std::is_sorted(mine.begin(), mine.end(),
                               [](ZoneId a, ZoneId b) { return a.Value < b.Value; }))
        << "PublishZoneScope reads this with a binary search";
    EXPECT_EQ(std::adjacent_find(mine.begin(), mine.end()), mine.end())
        << "a room both of them are near was offered twice";
}

// Two players in the same room produce one set each, not one shared one, and
// neither is missing anything.
TEST_F(NetOwnedFocusTest, TwoPeersInOneRoomAreEachOfferedIt)
{
    Pawn(PeerId{ 1 }, 4);
    Pawn(PeerId{ 2 }, 4);
    Step();

    EXPECT_EQ(Focus.Interest().size(), 2u);
    EXPECT_TRUE(Holds(InterestOf(Focus, PeerId{ 1 }), 4));
    EXPECT_TRUE(Holds(InterestOf(Focus, PeerId{ 2 }), 4));
}

// Interest follows the pawn, which is what makes a revoke happen at all.
TEST_F(NetOwnedFocusTest, InterestFollowsThePeerAcrossTheChain)
{
    const EntityId pawn = Pawn(PeerId{ 1 }, 0);
    Step();
    ASSERT_TRUE(Holds(InterestOf(Focus, PeerId{ 1 }), 0));

    World& world = Rig.World().Entities();
    for (int zone = 1; zone <= 4; ++zone)
    {
        world.TryGet<WorldTransform>(pawn)->Value.Position = InZone(zone);
        Step();
    }

    const std::vector<ZoneId> mine = InterestOf(Focus, PeerId{ 1 });
    EXPECT_TRUE(Holds(mine, 4));
    EXPECT_FALSE(Holds(mine, 0)) << "a peer is still offered a room it walked out of";
}

// A client computes no interest at all: it is not the one deciding what anybody
// may be told about.
TEST_F(NetOwnedFocusTest, AClientOffersNobodyAnything)
{
    Pawn(PeerId{ 1 }, 0);
    Step(NetSessionRole::Client);

    EXPECT_TRUE(Focus.Interest().empty());
}

//=============================================================================
// Loading what the authority granted
//
// The other side of the same conversation. A client streams around its own
// pawn, and in steady state that agrees with what it was granted; these are the
// cases where it does not.
//=============================================================================

namespace
{
    bool PinnedHere(const NetGrantedResidency& granted, int index)
    {
        const std::span<const ZoneId> pinned = granted.Pinned();
        return std::find(pinned.begin(), pinned.end(), ZoneAt(index))
               != pinned.end();
    }
}

// The deadlock this exists to prevent: a player somewhere their own policy has
// no reason to want. The authority withholds the room until it is acked, and
// the ack waits on a load nothing asked for.
TEST_F(NetOwnedFocusTest, AGrantedRoomIsLoadedEvenWithNoLocalReasonToWantIt)
{
    // This client is standing at one end of the chain, so its own policy wants
    // rooms 0 and 1 and nothing else. Room 5 is somewhere it has no reason to
    // load and the authority is waiting to be told it holds.
    Rig.SetFocusToZone(0);
    NetZoneScope scope;
    NetGrantedResidency granted;
    EXPECT_TRUE(scope.Grant(ZoneAt(5)));

    for (int frame = 0; frame < 4; ++frame)
    {
        granted.Update(scope, Rig.Partition());
        Rig.StepFrame();
    }
    Rig.SettleLoads();

    EXPECT_TRUE(Resident(5)) << "a granted room was never loaded, so it can never "
                                "be acked, so it is never filled";
    EXPECT_TRUE(PinnedHere(granted, 5));
}

// A grant is enough. Waiting for the ack would be waiting for the load this is
// meant to cause.
TEST_F(NetOwnedFocusTest, AGrantIsEnoughToStartLoading)
{
    NetZoneScope scope;
    NetGrantedResidency granted;
    (void)scope.Grant(ZoneAt(2));

    granted.Update(scope, Rig.Partition());

    EXPECT_TRUE(PinnedHere(granted, 2));
}

// And the pin comes back off, or a client keeps every room it ever visited
// resident for the rest of the session.
TEST_F(NetOwnedFocusTest, ARevokedRoomIsUnpinnedAndLetGo)
{
    Rig.SetFocusToZone(0);
    NetZoneScope scope;
    NetGrantedResidency granted;
    (void)scope.Grant(ZoneAt(5));
    for (int frame = 0; frame < 4; ++frame)
    {
        granted.Update(scope, Rig.Partition());
        Rig.StepFrame();
    }
    Rig.SettleLoads();
    ASSERT_TRUE(Resident(5));

    EXPECT_TRUE(scope.Revoke(ZoneAt(5)));
    for (int frame = 0; frame < 4; ++frame)
    {
        granted.Update(scope, Rig.Partition());
        Rig.StepFrame();
    }
    Rig.SettleLoads();

    EXPECT_FALSE(PinnedHere(granted, 5));
    EXPECT_FALSE(Resident(5)) << "a client holds every room it was ever granted";
}

// One room let go leaves the others where they were.
TEST_F(NetOwnedFocusTest, UnpinningOneGrantLeavesTheOthers)
{
    NetZoneScope scope;
    NetGrantedResidency granted;
    (void)scope.Grant(ZoneAt(2));
    (void)scope.Grant(ZoneAt(5));
    granted.Update(scope, Rig.Partition());
    ASSERT_EQ(granted.Pinned().size(), 2u);

    (void)scope.Revoke(ZoneAt(2));
    granted.Update(scope, Rig.Partition());

    EXPECT_FALSE(PinnedHere(granted, 2));
    EXPECT_TRUE(PinnedHere(granted, 5));
}
