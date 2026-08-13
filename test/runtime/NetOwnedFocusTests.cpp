#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/NetOwnedFocus.h>
#include <net/NetOwnership.h>
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
