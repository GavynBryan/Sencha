#include <gtest/gtest.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

//=============================================================================
// Which zone a replicated entity is in
//
// Everything the authority publishes today is relevant to everybody, so nothing
// yet reads this. It is the fact per-peer zone scope is written against: a grant
// names a zone, so what an entity is owed to depends on the zone it is in, and
// the only place that can be read cheaply is the walk the change store already
// makes.
//=============================================================================

namespace
{
    constexpr ZoneId kFirstZone{ 0xa1 };
    constexpr ZoneId kSecondZone{ 0xa2 };

    // The engine's own component table, so the walk sees exactly the columns it
    // sees in a session. A base rather than members, because RuntimeWorld
    // applies the schema as it is constructed and so must come after it.
    struct AttributionTables
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;

        AttributionTables()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Layout.Seal();
        }
    };

    struct AttributionFixture : AttributionTables
    {
        RuntimeWorld Runtime;

        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        std::uint64_t Generation = 0;

        AttributionFixture()
            : Runtime(Schema)
        {
        }

        World& Entities() { return Runtime.Entities(); }

        // A replicated entity, optionally moved out of the persistent partition
        // into a resident zone.
        EntityId Replicated(ZoneId zone = {})
        {
            World& world = Entities();
            const EntityId entity = world.CreateEntity();
            world.AddComponent<NetReplicated>(entity, NetReplicated{});
            world.AddComponent<LocalTransform>(entity, LocalTransform{});
            if (zone.IsValid())
            {
                EXPECT_TRUE(Runtime.MoveEntityToZone(entity, zone));
            }
            return entity;
        }

        void Publish(bool withPartitionRuntime = true)
        {
            Changes.Update(Entities(), Layout, Identity, ++Generation,
                           withPartitionRuntime ? &Runtime : nullptr);
        }

        [[nodiscard]] ZoneId ZoneOf(EntityId entity)
        {
            const ReplicationChangeStore::EntityState* state =
                Changes.Find(Identity.IdFor(entity));
            return state == nullptr ? ZoneId{} : state->Zone;
        }
    };
}

TEST(ReplicationZoneAttribution, AnEntityInAStreamedZoneIsAttributedToIt)
{
    AttributionFixture fixture;
    fixture.Runtime.AttachZone(kFirstZone);
    const EntityId entity = fixture.Replicated(kFirstZone);

    fixture.Publish();

    EXPECT_EQ(fixture.ZoneOf(entity), kFirstZone);
}

// The persistent partition is not a zone and has no id. Everything a session
// spawns lives there, and no grant gates it because every peer has it already.
TEST(ReplicationZoneAttribution, AnEntityInThePersistentPartitionHasNoZone)
{
    AttributionFixture fixture;
    const EntityId entity = fixture.Replicated();

    fixture.Publish();

    EXPECT_FALSE(fixture.ZoneOf(entity).IsValid());
}

// The answer is per chunk, and two partitions are never one chunk. Resolved
// once for the whole walk instead, both of these would read as the same zone.
TEST(ReplicationZoneAttribution, EntitiesInDifferentZonesAreAttributedSeparately)
{
    AttributionFixture fixture;
    fixture.Runtime.AttachZone(kFirstZone);
    fixture.Runtime.AttachZone(kSecondZone);
    const EntityId here = fixture.Replicated(kFirstZone);
    const EntityId there = fixture.Replicated(kSecondZone);
    const EntityId persistent = fixture.Replicated();

    fixture.Publish();

    EXPECT_EQ(fixture.ZoneOf(here), kFirstZone);
    EXPECT_EQ(fixture.ZoneOf(there), kSecondZone);
    EXPECT_FALSE(fixture.ZoneOf(persistent).IsValid());
}

// Read every pass rather than once. An entity crossing a boundary changes which
// peers it is relevant to, and a stale attribution is the shape of a defect
// where somebody keeps receiving a room they have walked out of.
TEST(ReplicationZoneAttribution, AnEntityThatChangesZonesIsReattributed)
{
    AttributionFixture fixture;
    fixture.Runtime.AttachZone(kFirstZone);
    fixture.Runtime.AttachZone(kSecondZone);
    const EntityId entity = fixture.Replicated(kFirstZone);
    fixture.Publish();
    ASSERT_EQ(fixture.ZoneOf(entity), kFirstZone);

    ASSERT_TRUE(fixture.Runtime.MoveEntityToZone(entity, kSecondZone));
    fixture.Publish();

    EXPECT_EQ(fixture.ZoneOf(entity), kSecondZone);
}

// The walk visits chunks, and a persistent one reached after a zoned one must
// not inherit its answer. Given a second component so the two land in separate
// archetypes and the persistent one is reached last, which is the arrangement
// that tells a per-chunk answer apart from one carried forward.
TEST(ReplicationZoneAttribution, APersistentEntityDoesNotInheritAnEarlierChunksZone)
{
    AttributionFixture fixture;
    fixture.Runtime.AttachZone(kFirstZone);
    const EntityId zoned = fixture.Replicated(kFirstZone);

    World& world = fixture.Entities();
    const EntityId persistent = fixture.Replicated();
    world.AddComponent<WorldTransform>(persistent, WorldTransform{});

    fixture.Publish();

    EXPECT_EQ(fixture.ZoneOf(zoned), kFirstZone);
    EXPECT_FALSE(fixture.ZoneOf(persistent).IsValid());
}

// The other direction, which is not symmetric to write: attribution has to be
// cleared, not only set. An entity that returns to the persistent partition is
// relevant to everybody again, and a stale zone would leave it owed only to
// whoever still holds a room it has left.
TEST(ReplicationZoneAttribution, AnEntityLeavingEveryZoneStopsBeingAttributed)
{
    AttributionFixture fixture;
    fixture.Runtime.AttachZone(kFirstZone);
    const EntityId entity = fixture.Replicated(kFirstZone);
    fixture.Publish();
    ASSERT_EQ(fixture.ZoneOf(entity), kFirstZone);

    ASSERT_TRUE(fixture.Runtime.MoveEntityToPersistent(entity));
    fixture.Publish();

    EXPECT_FALSE(fixture.ZoneOf(entity).IsValid());
}

// A game with one level has no partition runtime to ask, and the honest answer
// there is that nothing is in a zone -- not that the question failed.
TEST(ReplicationZoneAttribution, WithoutAPartitionRuntimeEverythingReadsPersistent)
{
    AttributionFixture fixture;
    fixture.Runtime.AttachZone(kFirstZone);
    const EntityId entity = fixture.Replicated(kFirstZone);

    fixture.Publish(/*withPartitionRuntime=*/false);

    EXPECT_FALSE(fixture.ZoneOf(entity).IsValid());
}
