// The first consumer of persistent entity identity: a zone that streams out
// and back must not resurrect entities the simulation destroyed. The store's
// diff mechanics are covered directly, then the full path — import, destroy,
// detach-capture, re-import suppression — through RuntimeWorld and the real
// package importer. RestreamedZoneMatchesAFreshAttach pins the complement: a
// zone with no recorded deviation restreams verbatim.

#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <zone/ZoneLoadPackage.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace
{
WorldComponentSchema IdentityOnlySchema()
{
    WorldComponentSchema schema;
    schema.Add<PersistentIdComponent>();
    schema.Seal();
    return schema;
}

ZoneParticipation LogicOnly()
{
    ZoneParticipation participation;
    participation.Logic = true;
    return participation;
}

void FinishResidency(RuntimeWorld& runtime)
{
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();
}

ZoneLoadPackage MakeIdentifiedPackage(ZoneId zone,
                                      std::span<const PersistentEntityId> ids)
{
    ZoneLoadPackage package(zone);
    for (const PersistentEntityId id : ids)
    {
        const ZoneLocalEntityId local = package.CreateEntity();
        EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(local, { id }));
        EXPECT_TRUE(package.SetPersistentId(local, id));
    }
    return package;
}

std::size_t AliveCount(const World& world)
{
    return world.GetAliveEntities().size();
}
} // namespace

TEST(ZoneStateStore, CaptureDiffsAuthoredAgainstLive)
{
    ZoneStateStore store;
    const std::array authored{ PersistentEntityId{ 0xa }, PersistentEntityId{ 0xb },
                               PersistentEntityId{ 0xc } };
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);

    const std::array live{ PersistentEntityId{ 0xb }, PersistentEntityId{ 0xc } };
    store.RecordDetachCapture(ZoneId{ 1 }, live);

    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xb }));
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 1 }), 1u);
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 2 }, PersistentEntityId{ 0xa }));
}

TEST(ZoneStateStore, CaptureAccumulatesAcrossResidencies)
{
    ZoneStateStore store;
    const std::array authored{ PersistentEntityId{ 0xa }, PersistentEntityId{ 0xb },
                               PersistentEntityId{ 0xc } };

    // First residency destroys A. The re-import records the same authored set
    // (a property of the cooked artifact) and its live set lacks both the
    // suppressed A and the newly destroyed B.
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    const std::array firstLive{ PersistentEntityId{ 0xb }, PersistentEntityId{ 0xc } };
    store.RecordDetachCapture(ZoneId{ 1 }, firstLive);

    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    const std::array secondLive{ PersistentEntityId{ 0xc } };
    store.RecordDetachCapture(ZoneId{ 1 }, secondLive);

    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xb }));
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 1 }), 2u);
}

TEST(ZoneStateStore, CaptureWithoutAuthoredRecordIsIgnored)
{
    ZoneStateStore store;
    const std::array live{ PersistentEntityId{ 0xb } };
    store.RecordDetachCapture(ZoneId{ 7 }, live);
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 7 }), 0u);
}

TEST(ZoneStateStore, ClearRestoresAuthoredReplay)
{
    ZoneStateStore store;
    const std::array authored{ PersistentEntityId{ 0xa } };
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    store.RecordDetachCapture(ZoneId{ 1 }, {});
    ASSERT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));

    store.Clear();
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
}

TEST(ZoneStateMemory, DestroyedEntityStaysDestroyedAcrossRestream)
{
    const WorldComponentSchema schema = IdentityOnlySchema();
    RuntimeWorld runtime(schema);

    const ZoneId zone{ 42 };
    const PersistentEntityId pickup{ 0x100 };
    const PersistentEntityId door{ 0x200 };
    const std::array ids{ pickup, door };

    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(
        runtime, schema, MakeIdentifiedPackage(zone, ids), LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);

    auto* index = runtime.Entities().TryGetResource<PersistentEntityIndex>();
    ASSERT_NE(index, nullptr);
    const EntityId pickupEntity = index->TryResolve(pickup);
    ASSERT_TRUE(pickupEntity.IsValid());

    // The simulation consumes the pickup, then the player walks far enough
    // that the zone streams out.
    runtime.Entities().DestroyEntity(pickupEntity);
    ASSERT_TRUE(runtime.RequestDetach(zone));
    runtime.FlushLifecycleRequests();
    FinishResidency(runtime);
    ASSERT_EQ(AliveCount(runtime.Entities()), 0u);

    // Backtracking streams the zone back in from the same cooked package.
    ASSERT_TRUE(ImportZonePackage(
        runtime, schema, MakeIdentifiedPackage(zone, ids), LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);

    EXPECT_FALSE(index->TryResolve(pickup).IsValid())
        << "a consumed entity must not be resurrected by a restream";
    EXPECT_TRUE(index->TryResolve(door).IsValid());
    EXPECT_EQ(AliveCount(runtime.Entities()), 1u);
}

TEST(ZoneStateMemory, UntouchedZoneRestreamsVerbatim)
{
    const WorldComponentSchema schema = IdentityOnlySchema();
    RuntimeWorld runtime(schema);

    const ZoneId zone{ 43 };
    const std::array ids{ PersistentEntityId{ 0x300 }, PersistentEntityId{ 0x400 } };

    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(
        runtime, schema, MakeIdentifiedPackage(zone, ids), LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);

    ASSERT_TRUE(runtime.RequestDetach(zone));
    runtime.FlushLifecycleRequests();
    FinishResidency(runtime);

    ASSERT_TRUE(ImportZonePackage(
        runtime, schema, MakeIdentifiedPackage(zone, ids), LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);

    EXPECT_EQ(AliveCount(runtime.Entities()), ids.size());
}
