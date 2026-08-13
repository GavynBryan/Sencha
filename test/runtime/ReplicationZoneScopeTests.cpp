#include <gtest/gtest.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/NetZoneScope.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

#include <vector>

//=============================================================================
// The flow-control invariant
//
// An authority never sends entity state for a zone the peer has not confirmed
// it holds. Not a bandwidth decision: there is nowhere on that machine to put
// it, and an applier that creates the entities anyway builds a second copy of a
// room the client is about to load properly.
//
// The other half, which is easy to forget and impossible to notice: withholding
// alone is not safe. A client that stops receiving an entity does not forget
// it, so anything leaving a peer's scope has to be destroyed for that peer.
//=============================================================================

namespace
{
    constexpr ZoneId kNear{ 0xa1 };
    constexpr ZoneId kFar{ 0xa2 };
    // Resident but not streamed: a map loaded whole, which no policy names.
    constexpr ZoneId kUnstreamed{ 0xb7 };

    struct ScopeTables
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;

        ScopeTables()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Layout.Seal();
        }
    };

    // An authority whose entities live in real zone partitions, and a client
    // that receives them. Everything crosses as bytes.
    struct ScopedPair : ScopeTables
    {
        RuntimeWorld Authority;
        World Client;

        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        ReplicationPeerState Peer;
        ReplicationClientIdentity ClientIdentity;
        std::uint64_t Generation = 0;
        std::uint64_t Tick = 0;
        std::vector<std::byte> Scratch;
        std::vector<ZoneId> Streamed{ kNear, kFar };
        SnapshotWriteResult LastWrite;
        SnapshotApplyResult LastApply;

        ScopedPair()
            : Authority(Schema)
            , Scratch(64 * 1024)
        {
            Schema.Apply(Client);
            Authority.AttachZone(kNear);
            Authority.AttachZone(kFar);
            Authority.AttachZone(kUnstreamed);
        }

        EntityId Replicated(ZoneId zone)
        {
            World& world = Authority.Entities();
            const EntityId entity = world.CreateEntity();
            world.AddComponent<NetReplicated>(entity, NetReplicated{});
            world.AddComponent<LocalTransform>(entity, LocalTransform{});
            if (zone.IsValid())
            {
                EXPECT_TRUE(Authority.MoveEntityToZone(entity, zone));
            }
            return entity;
        }

        void Replicate()
        {
            ++Tick;
            Changes.Update(Authority.Entities(), Layout, Identity, ++Generation,
                           &Authority);

            SnapshotWriteRequest write;
            write.Changes = &Changes;
            write.Layout = &Layout;
            write.Peer = &Peer;
            write.Tick = Tick;
            write.Sequence = Peer.NextSnapshotSequence();
            write.StreamedZones = Streamed;

            LastWrite = ReplicationWriteSnapshot(write, Scratch);
            ASSERT_TRUE(LastWrite.Ok);

            SnapshotApplyRequest apply;
            apply.Target = &Client;
            apply.Schema = &Schema;
            apply.Layout = &Layout;
            apply.Identity = &ClientIdentity;

            LastApply = ReplicationApplySnapshot(
                apply, std::span(Scratch).subspan(0, LastWrite.BytesWritten));
            ASSERT_TRUE(LastApply.Ok())
                << SnapshotApplyErrorToString(LastApply.Error);

            NetSnapshotAck ack;
            ack.Observe(LastApply.Sequence);
            Peer.Acknowledge(ack);
        }

        // The client's copy of an authority entity, or invalid if it has none.
        [[nodiscard]] EntityId Mirror(EntityId entity) const
        {
            const NetEntityId id = Identity.TryFind(entity);
            return id.IsValid() ? ClientIdentity.TryResolve(id) : EntityId{};
        }

        [[nodiscard]] bool ClientHas(EntityId entity) const
        {
            const EntityId mirror = Mirror(entity);
            return mirror.IsValid() && Client.IsAlive(mirror);
        }

        void Grant(ZoneId zone) { (void)Peer.Zones().Grant(zone); }
        void Ack(ZoneId zone) { (void)Peer.Zones().Acknowledge(zone); }
        void Revoke(ZoneId zone) { (void)Peer.Zones().Revoke(zone); }
    };
}

TEST(ReplicationZoneScope, NothingIsSentForAZoneThePeerHasNotAcked)
{
    ScopedPair pair;
    const EntityId hidden = pair.Replicated(kFar);

    pair.Replicate();
    pair.Replicate();

    EXPECT_FALSE(pair.ClientHas(hidden))
        << "state arrived for a room the peer never confirmed it holds";
}

// A grant is an instruction to load, not permission to receive. Until the peer
// answers there is still nowhere to put it.
TEST(ReplicationZoneScope, AGrantAloneDoesNotOpenTheFlow)
{
    ScopedPair pair;
    const EntityId waiting = pair.Replicated(kNear);
    pair.Grant(kNear);

    pair.Replicate();
    pair.Replicate();

    EXPECT_FALSE(pair.ClientHas(waiting));
}

TEST(ReplicationZoneScope, AnAckedZonesEntitiesArrive)
{
    ScopedPair pair;
    const EntityId open = pair.Replicated(kNear);
    pair.Grant(kNear);
    pair.Ack(kNear);

    pair.Replicate();

    EXPECT_TRUE(pair.ClientHas(open));
}

// One room open does not open the others.
TEST(ReplicationZoneScope, AnAckedZoneDoesNotOpenTheOnesBesideIt)
{
    ScopedPair pair;
    const EntityId here = pair.Replicated(kNear);
    const EntityId there = pair.Replicated(kFar);
    pair.Grant(kNear);
    pair.Ack(kNear);

    pair.Replicate();

    EXPECT_TRUE(pair.ClientHas(here));
    EXPECT_FALSE(pair.ClientHas(there));
}

// Everything a session spawns lives in the persistent partition, and gating it
// would mean a player could not be told about the pawn they are driving.
TEST(ReplicationZoneScope, ThePersistentPartitionIsNeverGated)
{
    ScopedPair pair;
    const EntityId pawn = pair.Replicated(ZoneId{});

    pair.Replicate();

    EXPECT_TRUE(pair.ClientHas(pawn));
}

// A zone can be resident without being streamed: a map loaded whole is one
// partition holding the level, and no policy names it, so nothing would ever
// grant it. Gating it would withhold the entire level from every peer forever,
// with no message that could undo it.
TEST(ReplicationZoneScope, AZoneOutsideScopeControlIsNotGated)
{
    ScopedPair pair;
    const EntityId level = pair.Replicated(kUnstreamed);

    pair.Replicate();

    EXPECT_TRUE(pair.ClientHas(level))
        << "a level nothing streams was withheld from a peer that can never ask "
           "for it";
}

// Withholding alone is not safe. A client that stops receiving an entity keeps
// whatever it last saw, standing exactly where it was, forever.
TEST(ReplicationZoneScope, RevokingAZoneDestroysWhatThePeerHeldThere)
{
    ScopedPair pair;
    const EntityId inside = pair.Replicated(kNear);
    pair.Grant(kNear);
    pair.Ack(kNear);
    pair.Replicate();
    ASSERT_TRUE(pair.ClientHas(inside));

    pair.Revoke(kNear);
    pair.Replicate();

    EXPECT_FALSE(pair.ClientHas(inside))
        << "the peer was left holding a room it had been told to let go";
}

// The same rule reached the other way: the entity moves rather than the scope.
TEST(ReplicationZoneScope, AnEntityWalkingIntoAClosedRoomIsDestroyedForThePeer)
{
    ScopedPair pair;
    const EntityId walker = pair.Replicated(kNear);
    pair.Grant(kNear);
    pair.Ack(kNear);
    pair.Replicate();
    ASSERT_TRUE(pair.ClientHas(walker));

    ASSERT_TRUE(pair.Authority.MoveEntityToZone(walker, kFar));
    pair.Replicate();

    EXPECT_FALSE(pair.ClientHas(walker))
        << "a peer keeps watching something that walked into a room it does not "
           "have";
}

// And back again: a room reopened is seeded from nothing, because the client
// genuinely has nothing.
TEST(ReplicationZoneScope, AReopenedRoomIsSeededAgain)
{
    ScopedPair pair;
    const EntityId inside = pair.Replicated(kNear);
    pair.Grant(kNear);
    pair.Ack(kNear);
    pair.Replicate();
    pair.Revoke(kNear);
    pair.Replicate();
    ASSERT_FALSE(pair.ClientHas(inside));

    pair.Grant(kNear);
    pair.Ack(kNear);
    pair.Replicate();

    EXPECT_TRUE(pair.ClientHas(inside));
    const EntityId mirror = pair.Mirror(inside);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_TRUE(pair.Client.HasComponent<LocalTransform>(mirror))
        << "the entity came back as a delta against state the client had "
           "already dropped";
}
