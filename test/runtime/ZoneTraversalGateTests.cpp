#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/NetGrantedResidency.h>
#include <net/NetOwnedFocus.h>
#include <net/NetOwnership.h>
#include <net/NetReplicationComponents.h>
#include <net/NetZoneScope.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationRuntime.h>
#include <net/ReplicationSnapshot.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include "StreamingTraversalFixture.h"

#include <algorithm>
#include <memory>
#include <vector>

//=============================================================================
// Two players walking a streamed world at once
//
// G3's gate, run as a closed loop rather than as its separate halves. Every
// mechanism in the track is in it: an authority streaming around both players,
// each peer offered only its own neighbourhood, grants going out, clients
// loading what they were granted, acking when the room is actually attached,
// and state flowing only after that.
//
// The wire is left out on purpose -- NetZoneScopeTests carries the bytes over
// real sessions -- so what is left is deterministic and the failures are about
// the handoff rather than about timing.
//
// The assertion that matters is not "grants were sent". It is that a player
// walking a chain of rooms is never, at any step, missing something standing in
// the room they are in. A streaming handoff that is a frame late is a player
// walking into an empty room, and it would pass every test that only checks the
// two ends.
//=============================================================================

namespace
{
    using StreamingTraversal::Harness;
    using StreamingTraversal::ZoneAt;
    using StreamingTraversal::kZoneCount;
    using StreamingTraversal::kZoneSpan;

    Vec3d Inside(int index)
    {
        return Vec3d{ static_cast<float>(index * kZoneSpan + kZoneSpan * 0.5),
                      1.0f,
                      0.0f };
    }

    // One connected player, from the authority's side and its own.
    struct Player
    {
        PeerId Id;
        // The pawn the authority simulates for them, in the persistent
        // partition: a session's own spawns are never zone-gated, which is what
        // stops a peer waiting on a room to be told about its own body.
        EntityId Pawn;
        int Standing = 0;

        // The authority's record of what this peer has been told.
        ReplicationPeerState Baseline;
        // This player's own machine: its world, its streaming, and what it has
        // been granted.
        std::unique_ptr<Harness> Rig;
        ReplicationClientIdentity Identity;
        NetZoneScope Scope;
        NetGrantedResidency Granted;
    };

    class ZoneTraversalGateTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(Authority.LoadManifest().empty());
            for (const ZoneHeader& zone : Authority.Partition().Manifest().Zones)
                Streamed.push_back(zone.Id);
            Landmarks.assign(kZoneCount, EntityId{});
            Scratch.resize(64 * 1024);
        }

        // A replicated entity that lives in one room, so "can this player see
        // the room they are in" is a question with an answer.
        //
        // Placed after streaming has settled rather than during setup, because a
        // zone has no storage partition until the authority has loaded it -- and
        // an entity left in the persistent partition is one no scope gates,
        // which is a test that passes without testing anything.
        void PlaceLandmarks()
        {
            World& world = Authority.World().Entities();
            for (int index = 0; index < kZoneCount; ++index)
            {
                // Liveness, not just validity. A room the authority unloads
                // takes its contents with it, so a landmark is put back when
                // the room comes round again -- which is what a player walking
                // a loop actually encounters.
                if ((Landmarks[index].IsValid() && world.IsAlive(Landmarks[index]))
                    || !Authority.World().IsZoneResident(ZoneAt(index)))
                {
                    continue;
                }

                const EntityId entity = world.CreateEntity();
                world.AddComponent<NetReplicated>(entity, NetReplicated{});
                Transform3f placed;
                placed.Position = Inside(index);
                world.AddComponent<LocalTransform>(entity, LocalTransform{ placed });
                world.AddComponent<WorldTransform>(entity, WorldTransform{ placed });
                ASSERT_TRUE(
                    Authority.World().MoveEntityToZone(entity, ZoneAt(index)));
                Landmarks[index] = entity;
            }
        }

        Player& Join(int startZone)
        {
            Players.push_back(std::make_unique<Player>());
            Player& player = *Players.back();
            player.Id = PeerId{ static_cast<std::uint32_t>(Players.size()) };
            player.Standing = startZone;
            player.Rig = std::make_unique<Harness>(0, kZoneCount);
            EXPECT_TRUE(player.Rig->LoadManifest().empty());

            World& world = Authority.World().Entities();
            player.Pawn = world.CreateEntity();
            Transform3f placed;
            placed.Position = Inside(startZone);
            world.AddComponent<WorldTransform>(player.Pawn, WorldTransform{ placed });
            world.AddComponent<NetReplicated>(player.Pawn, NetReplicated{});
            NetSetOwner(world, player.Pawn, player.Id);
            return player;
        }

        void MoveTo(Player& player, int zoneIndex)
        {
            player.Standing = zoneIndex;
            World& world = Authority.World().Entities();
            world.TryGet<WorldTransform>(player.Pawn)->Value.Position =
                Inside(zoneIndex);
        }

        // One frame of the whole loop, in the order the engine runs it.
        void Frame()
        {
            World& world = Authority.World().Entities();

            // Streaming: the authority holds the union of everybody's
            // neighbourhoods and works out what each of them may be told about.
            Focus.Update(world, NetSessionRole::Host, Authority.Partition());
            const std::span<const NetPeerZoneInterest> interest = Focus.Interest();

            // Grants and revokes, delivered straight into each client's scope.
            for (std::unique_ptr<Player>& held : Players)
            {
                Player& player = *held;
                std::span<const ZoneId> wanted;
                for (const NetPeerZoneInterest& record : interest)
                {
                    if (record.Peer == player.Id)
                        wanted = record.Zones;
                }

                std::vector<ZoneId> revoking;
                for (const NetZoneScope::Entry& entry :
                     player.Baseline.Zones().Entries())
                {
                    if (!std::binary_search(
                            wanted.begin(), wanted.end(), entry.Zone,
                            [](ZoneId a, ZoneId b) { return a.Value < b.Value; }))
                    {
                        revoking.push_back(entry.Zone);
                    }
                }
                for (const ZoneId zone : revoking)
                {
                    (void)player.Baseline.Zones().Revoke(zone);
                    (void)player.Scope.Revoke(zone);
                }
                for (const ZoneId zone : wanted)
                {
                    if (player.Baseline.Zones().Grant(zone))
                        (void)player.Scope.Grant(zone);
                }
            }

            Authority.StepFrame();

            // Each client loads what it was granted, and confirms a room only
            // once its own world actually has it.
            for (std::unique_ptr<Player>& held : Players)
            {
                Player& player = *held;
                player.Rig->Partition().SetFocus(Inside(player.Standing));
                player.Granted.Update(player.Scope, player.Rig->Partition());
                player.Rig->StepFrame();

                std::vector<ZoneId> ready;
                for (const NetZoneScope::Entry& entry : player.Scope.Entries())
                {
                    if (entry.State == NetZoneScopeState::Granted
                        && player.Rig->World().IsZoneResident(entry.Zone))
                    {
                        ready.push_back(entry.Zone);
                    }
                }
                for (const ZoneId zone : ready)
                {
                    (void)player.Scope.Acknowledge(zone);
                    EXPECT_TRUE(player.Baseline.Zones().Acknowledge(zone));
                }
            }

            Publish();
        }

        void Publish()
        {
            Changes.Update(Authority.World().Entities(), Layout(), Identity,
                           ++Generation, &Authority.World());
            ++Tick;

            for (std::unique_ptr<Player>& held : Players)
            {
                Player& player = *held;

                SnapshotWriteRequest write;
                write.Changes = &Changes;
                write.Layout = &Layout();
                write.Peer = &player.Baseline;
                write.OwnerPeer = player.Id.Value;
                write.Tick = Tick;
                write.Sequence = player.Baseline.NextSnapshotSequence();
                write.StreamedZones = Streamed;

                const SnapshotWriteResult written =
                    ReplicationWriteSnapshot(write, Scratch);
                ASSERT_TRUE(written.Ok);
                ++Published;

                SnapshotApplyRequest apply;
                apply.Target = &player.Rig->World().Entities();
                apply.Schema = &SchemaOf();
                apply.Layout = &Layout();
                apply.Identity = &player.Identity;

                const SnapshotApplyResult applied = ReplicationApplySnapshot(
                    apply, std::span(Scratch).subspan(0, written.BytesWritten));
                ASSERT_TRUE(applied.Ok())
                    << SnapshotApplyErrorToString(applied.Error);

                if (applied.Complete())
                {
                    NetSnapshotAck ack;
                    ack.Observe(applied.Sequence);
                    player.Baseline.Acknowledge(ack);
                }
            }
        }

        // Whether this player's machine currently holds the landmark of a room.
        [[nodiscard]] bool Sees(Player& player, int zoneIndex)
        {
            if (!Landmarks[zoneIndex].IsValid())
                return false;
            const NetEntityId id = Identity.TryFind(Landmarks[zoneIndex]);
            if (!id.IsValid())
                return false;
            const EntityId mirror = player.Identity.TryResolve(id);
            return mirror.IsValid()
                   && player.Rig->World().Entities().IsAlive(mirror);
        }

        // The engine's own component table, shared by every world here.
        static const WorldComponentSchema& SchemaOf()
        {
            static const WorldComponentSchema& schema = Tables().Schema;
            return schema;
        }
        static const ReplicationLayout& Layout() { return Tables().Layout; }

        struct SharedTables
        {
            WorldComponentSchema Schema;
            ReplicationLayout Layout;
            SharedTables()
            {
                ComponentRegistrar components(&Schema, nullptr, &Layout);
                RegisterEngineComponents(components);
                Schema.Seal();
                Layout.Seal();
            }
        };
        static SharedTables& Tables()
        {
            static SharedTables tables;
            return tables;
        }

        // Frames, placing a landmark in each room as it becomes resident.
        void Settle(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                Frame();
                PlaceLandmarks();
            }
        }

        // Wide enough to hold both players' neighbourhoods at once. The cap no
        // longer strands a crossing when it cannot (see ZoneTraversalCap), but
        // it does still evict, and this gate is about the handoff rather than
        // about what a tight budget does to it.
        Harness Authority{ 0, kZoneCount };
        NetOwnedFocus Focus;
        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        std::vector<std::unique_ptr<Player>> Players;
        std::vector<EntityId> Landmarks;
        std::vector<ZoneId> AuthorityZones;
        std::vector<ZoneId> Streamed;
        std::vector<std::byte> Scratch;
        std::uint64_t Generation = 0;
        std::uint64_t Tick = 0;
        std::uint32_t Published = 0;
    };
}

// Settle: a player standing still ends up holding the room they are in and
// seeing what is in it. Everything below is measured against this working.
TEST_F(ZoneTraversalGateTest, APlayerStandingStillSeesTheRoomTheyAreIn)
{
    Player& first = Join(0);

    Settle(12);

    EXPECT_EQ(first.Scope.StateOf(ZoneAt(0)), NetZoneScopeState::Acked);
    EXPECT_TRUE(Sees(first, 0)) << "the room this player is standing in is empty";
}

// The gate. Two players walk the chain in opposite directions, and at every
// single step each of them can see the room they are standing in.
//
// Checked every step rather than at the ends, because a handoff that is a frame
// late is a player walking into an empty room and both ends would still look
// right.
TEST_F(ZoneTraversalGateTest, TwoPlayersCrossTheWorldWithoutEverLosingTheirRoom)
{
    Player& outbound = Join(0);
    Player& inbound = Join(kZoneCount - 1);

    Settle(12);
    ASSERT_TRUE(Sees(outbound, 0));
    ASSERT_TRUE(Sees(inbound, kZoneCount - 1));

    for (int step = 1; step < kZoneCount; ++step)
    {
        MoveTo(outbound, step);
        MoveTo(inbound, kZoneCount - 1 - step);

        // Long enough for a grant, a load, an ack, and a snapshot -- which is
        // the round trip this whole track exists to make bounded.
        Settle(8);

        EXPECT_TRUE(Sees(outbound, outbound.Standing))
            << "player one walked into room " << outbound.Standing
            << " and it was empty";
        EXPECT_TRUE(Sees(inbound, inbound.Standing))
            << "player two walked into room " << inbound.Standing
            << " and it was empty";
    }
}

// The other half of the same walk: what each player stops being told about.
// Two players at opposite ends of a chain must not each be holding the whole
// world, or scoped replication has cost a round trip and bought nothing.
TEST_F(ZoneTraversalGateTest, NeitherPlayerEndsUpHoldingTheWholeWorld)
{
    Player& first = Join(0);
    Player& second = Join(kZoneCount - 1);

    // Both ends are resident on the authority, because somebody is standing in
    // each. So the only reason either player is missing the far end is scope,
    // rather than the room not existing to be sent.
    Settle(16);
    ASSERT_TRUE(Sees(first, 0));
    ASSERT_TRUE(Sees(second, kZoneCount - 1));

    EXPECT_FALSE(Sees(first, kZoneCount - 1))
        << "a player at one end of the world is being sent the other end";
    EXPECT_FALSE(Sees(second, 0));
    EXPECT_LT(first.Scope.Size(), static_cast<std::size_t>(kZoneCount));
    EXPECT_LT(second.Scope.Size(), static_cast<std::size_t>(kZoneCount));
}

// A player passing through leaves nothing behind. Without the destroy on scope
// exit the client keeps every room it has ever walked through, and the point of
// the whole mechanism is lost quietly rather than loudly.
//
// A second player stays behind in the room the first one leaves. That is what
// makes this a test of scope rather than of unloading: the room stays resident
// on the authority and its landmark stays alive, so the only thing that can
// take it off the walker's machine is having stopped being told about it.
TEST_F(ZoneTraversalGateTest, WhatAPlayerWalksPastIsLetGoAgain)
{
    Player& walker = Join(0);
    Player& resident = Join(0);
    Settle(12);
    ASSERT_TRUE(Sees(walker, 0));
    ASSERT_TRUE(Sees(resident, 0));

    for (int step = 1; step < kZoneCount; ++step)
    {
        MoveTo(walker, step);
        Settle(8);
    }

    EXPECT_TRUE(Sees(walker, kZoneCount - 1));
    EXPECT_FALSE(Sees(walker, 0))
        << "a player is still holding the room they started in, eight rooms ago";
    EXPECT_TRUE(Sees(resident, 0))
        << "the player who never moved lost the room they are standing in";
}

// A publish never stalls waiting on a load. The authority's cadence is its own,
// and a peer that is slow to ack costs that peer freshness rather than costing
// the session ticks.
TEST_F(ZoneTraversalGateTest, TheAuthorityNeverStopsPublishingWhileAPeerLoads)
{
    Player& first = Join(0);
    Player& second = Join(4);

    const std::uint32_t before = Published;
    Settle(10);

    EXPECT_EQ(Published - before, 20u)
        << "a frame went by without every peer being written a snapshot";
    EXPECT_GT(first.Scope.Size(), 0u);
    EXPECT_GT(second.Scope.Size(), 0u);
}

//=============================================================================
// Several players against one budget
//
// ResidentZoneCap bounds the merged demand of every focus source at once, and
// it was sized when there could only be one. Two players in different parts of
// a world ask for more rooms than a budget meant for one, and what happens then
// used to be worse than a full budget: a crossing is held back when its
// destination is not resident, the destination lost the eviction because it was
// only a neighbour of a focus rather than a focus itself, and it stayed a
// neighbour precisely because the crossing that would promote it was the one
// being held back. Players still advanced, a room per attempt, while the
// world's idea of where they were trailed where they actually were -- so
// residency and relevance were both computed for rooms they had left.
//
// A room a source is part way into now counts as somewhere it is, not somewhere
// it can see, so the cap cannot evict what a crossing is waiting on.
//=============================================================================

// The case that used to fail: a tight budget with more players in it than it
// was sized for, and the world keeping up with both anyway.
TEST(ZoneTraversalCap, ASingleFocusBudgetStillKeepsUpWithTwoPlayers)
{
    Harness rig{ 0, 4 };
    ASSERT_TRUE(rig.LoadManifest().empty());

    constexpr FocusSourceId kOutbound{ 0x8000'0001 };
    constexpr FocusSourceId kInbound{ 0x8000'0002 };

    rig.Partition().SetFocus(kOutbound, Inside(0));
    rig.Partition().SetFocus(kInbound, Inside(kZoneCount - 1));
    for (int frame = 0; frame < 12; ++frame)
        rig.StepFrame();
    rig.SettleLoads();
    ASSERT_EQ(rig.Partition().FocusZone(kInbound), ZoneAt(kZoneCount - 1));

    // The position is re-set every frame, the way a session does it from a
    // pawn's transform: a held-back crossing only retries when a new position
    // arrives, so a caller that sets one and then waits is waiting forever.
    for (int step = 1; step < 4; ++step)
    {
        for (int frame = 0; frame < 8; ++frame)
        {
            rig.Partition().SetFocus(kOutbound, Inside(step));
            rig.Partition().SetFocus(kInbound, Inside(kZoneCount - 1 - step));
            rig.StepFrame();
        }
        rig.SettleLoads();
    }

    EXPECT_EQ(rig.Partition().FocusZone(kOutbound), ZoneAt(3))
        << "the world lost track of where the first player is";
    EXPECT_EQ(rig.Partition().FocusZone(kInbound), ZoneAt(kZoneCount - 4))
        << "the world lost track of where the second player is";
}

// A crossing still waits for its destination -- the immunity is about not
// evicting the room, not about walking into one that is not there.
TEST(ZoneTraversalCap, ACrossingStillWaitsForARoomThatIsNotLoadedYet)
{
    Harness rig{ 0, 4 };
    ASSERT_TRUE(rig.LoadManifest().empty());

    rig.Partition().SetFocus(kPrimaryFocusSource, Inside(0));
    for (int frame = 0; frame < 12; ++frame)
        rig.StepFrame();
    rig.SettleLoads();

    // Straight into a room two doorways away, which no neighbour preload has
    // brought in. The sweep refuses to put the focus somewhere unloaded.
    rig.Partition().SetFocus(kPrimaryFocusSource, Inside(4));
    rig.StepFrame();

    EXPECT_NE(rig.Partition().FocusZone(kPrimaryFocusSource), ZoneAt(4))
        << "a focus landed in a room the world had not loaded";
}
