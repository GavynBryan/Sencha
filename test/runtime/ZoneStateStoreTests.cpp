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
#include <world/build/EntityBuildPackage.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
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

EntityBuildPackage MakeIdentifiedPackage(std::span<const PersistentEntityId> ids)
{
    EntityBuildPackage package;
    for (const PersistentEntityId id : ids)
    {
        const PackageEntityId local = package.CreateEntity();
        EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(local, { id }));
        EXPECT_TRUE(package.SetPersistentId(local, id));
    }
    return package;
}

std::size_t AliveCount(const World& world)
{
    return world.GetAliveEntities().size();
}

std::vector<AuthoredEntityState> Authored(std::initializer_list<std::uint64_t> ids,
                                          ZoneStateForgetting forgotten = ZoneStateForgetting::Never)
{
    std::vector<AuthoredEntityState> authored;
    for (const std::uint64_t id : ids)
        authored.push_back(AuthoredEntityState{ PersistentEntityId{ id }, forgotten });
    return authored;
}
} // namespace

TEST(ZoneStateStore, CaptureDiffsAuthoredAgainstLive)
{
    ZoneStateStore store;
    const std::vector authored = Authored({ 0xa, 0xb, 0xc });
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);

    const std::array live{ PersistentEntityId{ 0xb }, PersistentEntityId{ 0xc } };
    store.RecordCapture(ZoneId{ 1 }, live, ZoneCapture::Detach);

    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xb }));
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 1 }), 1u);
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 2 }, PersistentEntityId{ 0xa }));
}

TEST(ZoneStateStore, CaptureAccumulatesAcrossResidencies)
{
    ZoneStateStore store;
    const std::vector authored = Authored({ 0xa, 0xb, 0xc });

    // First residency destroys A. The re-import records the same authored set
    // (a property of the cooked artifact) and its live set lacks both the
    // suppressed A and the newly destroyed B.
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    const std::array firstLive{ PersistentEntityId{ 0xb }, PersistentEntityId{ 0xc } };
    store.RecordCapture(ZoneId{ 1 }, firstLive, ZoneCapture::Detach);

    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    const std::array secondLive{ PersistentEntityId{ 0xc } };
    store.RecordCapture(ZoneId{ 1 }, secondLive, ZoneCapture::Detach);

    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xb }));
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 1 }), 2u);
}

TEST(ZoneStateStore, CaptureWithoutAuthoredRecordIsIgnored)
{
    ZoneStateStore store;
    const std::array live{ PersistentEntityId{ 0xb } };
    store.RecordCapture(ZoneId{ 7 }, live, ZoneCapture::Detach);
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 7 }), 0u);
}

TEST(ZoneStateStore, RepeatedCaptureWithoutReimportKeepsTheDeviation)
{
    ZoneStateStore store;
    const std::vector authored = Authored({ 0xa, 0xb });
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    const std::array live{ PersistentEntityId{ 0xb } };
    store.RecordCapture(ZoneId{ 1 }, live, ZoneCapture::Detach);
    ASSERT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));

    // Only an import supplies a baseline, so a capture without one has nothing
    // to diff. Recomputing from an empty baseline would read as "everything
    // survived" and quietly resurrect A.
    store.RecordCapture(ZoneId{ 1 }, {}, ZoneCapture::Detach);

    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
    EXPECT_EQ(store.RecordedDestroyedCount(ZoneId{ 1 }), 1u);
}

TEST(ZoneStateStore, ClearRestoresAuthoredReplay)
{
    ZoneStateStore store;
    const std::vector authored = Authored({ 0xa });
    store.RecordAuthoredSet(ZoneId{ 1 }, authored);
    store.RecordCapture(ZoneId{ 1 }, {}, ZoneCapture::Detach);
    ASSERT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));

    store.Clear();
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
}

TEST(ZoneStateStore, WhatIsForgottenAtDetachIsNotRecordedWhenTheZoneLeaves)
{
    ZoneStateStore store;
    store.RecordAuthoredSet(ZoneId{ 1 }, Authored({ 0xa }, ZoneStateForgetting::AtDetach));
    store.RecordCapture(ZoneId{ 1 }, {}, ZoneCapture::Live);
    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa })) << "gone while the zone stays";

    store.RecordCapture(ZoneId{ 1 }, {}, ZoneCapture::Detach);
    EXPECT_FALSE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
}

TEST(ZoneStateStore, ALiveCaptureKeepsTheAuthoredSetForTheNext)
{
    ZoneStateStore store;
    store.RecordAuthoredSet(ZoneId{ 1 }, Authored({ 0xa, 0xb }));
    const std::array first{ PersistentEntityId{ 0xa }, PersistentEntityId{ 0xb } };
    store.RecordCapture(ZoneId{ 1 }, first, ZoneCapture::Live);
    const std::array second{ PersistentEntityId{ 0xb } };
    store.RecordCapture(ZoneId{ 1 }, second, ZoneCapture::Live);
    EXPECT_TRUE(store.IsRecordedDestroyed(ZoneId{ 1 }, PersistentEntityId{ 0xa }));
}

TEST(ZoneStateStore, OnlyWhatIsNeverForgottenIsDurableAndItExportsInIdOrder)
{
    ZoneStateStore store;
    std::vector authored = Authored({ 0x30, 0x10, 0x20 });
    authored.push_back(AuthoredEntityState{ PersistentEntityId{ 0x40 }, ZoneStateForgetting::AtRestore });
    store.RecordAuthoredSet(ZoneId{ 9 }, authored);
    store.RecordAuthoredSet(ZoneId{ 3 }, Authored({ 0x50 }));
    const std::array bytes{ std::byte{ 1 }, std::byte{ 2 } };
    store.RecordComponentState(ZoneId{ 9 }, PersistentEntityId{ 0x20 }, ComponentTypeId{ 7 }, 2, bytes);
    store.RecordComponentState(ZoneId{ 9 }, PersistentEntityId{ 0x40 }, ComponentTypeId{ 7 }, 2, bytes);
    store.RecordCapture(ZoneId{ 9 }, std::array{ PersistentEntityId{ 0x20 } }, ZoneCapture::Live);
    store.RecordCapture(ZoneId{ 3 }, {}, ZoneCapture::Live);

    const std::vector<DurableZoneState> durable = store.ExportDurable();
    ASSERT_EQ(durable.size(), 2u);
    EXPECT_EQ(durable[0].Zone, ZoneId{ 3 });
    EXPECT_EQ(durable[1].Zone, ZoneId{ 9 });
    EXPECT_EQ(durable[1].Destroyed, (std::vector{ PersistentEntityId{ 0x10 }, PersistentEntityId{ 0x30 } }))
        << "0x40 is forgotten at restore and 0x20 lives";
    ASSERT_EQ(durable[1].Components.size(), 1u);
    EXPECT_EQ(durable[1].Components[0].Entity, PersistentEntityId{ 0x20 });
    EXPECT_EQ(durable[1].Components[0].Version, 2);

    ZoneStateStore restored;
    restored.RecordAuthoredSet(ZoneId{ 5 }, Authored({ 0x60 }));
    restored.RecordCapture(ZoneId{ 5 }, {}, ZoneCapture::Detach);
    restored.Install(durable);
    EXPECT_EQ(restored.ExportDurable(), durable);
    EXPECT_FALSE(restored.IsRecordedDestroyed(ZoneId{ 5 }, PersistentEntityId{ 0x60 })) << "install replaces";
}

TEST(ZoneStateMemory, AChildTakesItsParentsLifetimeAndIsForgottenAtRestore)
{
    WorldComponentSchema schema;
    schema.Add<PersistentIdComponent>();
    schema.Add<LocalTransform>();
    schema.Add<WorldTransform>();
    schema.Add<Parent>();
    schema.Add<ZoneStateLifetime>();
    schema.Seal();
    RuntimeWorld runtime(schema);

    const ZoneId zone{ 44 };
    const PersistentEntityId enemy{ 0x500 };
    const PersistentEntityId weapon{ 0x501 };
    const PersistentEntityId gate{ 0x600 };
    const auto package = [&] {
        EntityBuildPackage built;
        const PackageEntityId root = built.CreateEntity();
        EXPECT_TRUE(built.AddComponent<PersistentIdComponent>(root, { enemy }));
        EXPECT_TRUE(built.SetPersistentId(root, enemy));
        EXPECT_TRUE(built.AddComponent<ZoneStateLifetime>(root, { ZoneStateForgetting::AtRestore }));
        const PackageEntityId child = built.CreateEntity();
        EXPECT_TRUE(built.AddComponent<PersistentIdComponent>(child, { weapon }));
        EXPECT_TRUE(built.SetPersistentId(child, weapon));
        EXPECT_TRUE(built.SetParent(child, root));
        const PackageEntityId kept = built.CreateEntity();
        EXPECT_TRUE(built.AddComponent<PersistentIdComponent>(kept, { gate }));
        EXPECT_TRUE(built.SetPersistentId(kept, gate));
        return built;
    };

    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(runtime, schema, zone, package(), LogicOnly(), &error)) << error.Message;
    FinishResidency(runtime);
    auto* index = runtime.Entities().TryGetResource<PersistentEntityIndex>();
    runtime.Entities().DestroyEntity(index->TryResolve(weapon));
    runtime.Entities().DestroyEntity(index->TryResolve(enemy));
    runtime.Entities().DestroyEntity(index->TryResolve(gate));
    ASSERT_TRUE(runtime.RequestDetach(zone));
    runtime.FlushLifecycleRequests();
    FinishResidency(runtime);

    ZoneStateStore& store = runtime.Entities().GetResource<ZoneStateStore>();
    EXPECT_TRUE(store.IsRecordedDestroyed(zone, weapon)) << "gone for the session";
    const std::vector<DurableZoneState> durable = store.ExportDurable();
    ASSERT_EQ(durable.size(), 1u);
    EXPECT_EQ(durable[0].Destroyed, std::vector{ gate }) << "only the gate is never forgotten";
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
        runtime, schema, zone, MakeIdentifiedPackage(ids), LogicOnly(), &error))
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
        runtime, schema, zone, MakeIdentifiedPackage(ids), LogicOnly(), &error))
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
        runtime, schema, zone, MakeIdentifiedPackage(ids), LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);

    ASSERT_TRUE(runtime.RequestDetach(zone));
    runtime.FlushLifecycleRequests();
    FinishResidency(runtime);

    ASSERT_TRUE(ImportZonePackage(
        runtime, schema, zone, MakeIdentifiedPackage(ids), LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);

    EXPECT_EQ(AliveCount(runtime.Entities()), ids.size());
}

// The package carries identity twice — as import metadata consulted before the
// row exists, and as the component the index registers afterwards. They decide
// different things, so a package where they disagree would suppress under one
// identity and resolve under the other.
TEST(ZonePackageIdentity, MetadataDisagreeingWithTheComponentFailsImport)
{
    const WorldComponentSchema schema = IdentityOnlySchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    const PackageEntityId local = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent<PersistentIdComponent>(
        local, { PersistentEntityId{ 0xaaa } }));
    ASSERT_TRUE(package.SetPersistentId(local, PersistentEntityId{ 0xbbb }));

    ZoneImportError error;
    EXPECT_FALSE(ImportZonePackage(
        runtime, schema, ZoneId{ 51 }, package, LogicOnly(), &error));
    EXPECT_NE(error.Message.find("identity metadata"), std::string::npos)
        << error.Message;
    EXPECT_EQ(AliveCount(runtime.Entities()), 0u)
        << "a rejected package must leave no partially imported rows";
}

TEST(ZonePackageIdentity, MetadataWithoutTheComponentFailsImport)
{
    const WorldComponentSchema schema = IdentityOnlySchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    const PackageEntityId local = package.CreateEntity();
    ASSERT_TRUE(package.SetPersistentId(local, PersistentEntityId{ 0xccc }));

    ZoneImportError error;
    EXPECT_FALSE(ImportZonePackage(
        runtime, schema, ZoneId{ 52 }, package, LogicOnly(), &error));
    EXPECT_NE(error.Message.find("identity metadata"), std::string::npos)
        << error.Message;
}

TEST(ZonePackageIdentity, ComponentWithoutMetadataFailsImport)
{
    const WorldComponentSchema schema = IdentityOnlySchema();
    RuntimeWorld runtime(schema);

    // The dangerous direction: the entity would register in the index but never
    // be reachable by suppression, so destroying it would not survive a restream.
    EntityBuildPackage package;
    const PackageEntityId local = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent<PersistentIdComponent>(
        local, { PersistentEntityId{ 0xddd } }));

    ZoneImportError error;
    EXPECT_FALSE(ImportZonePackage(
        runtime, schema, ZoneId{ 53 }, package, LogicOnly(), &error));
    EXPECT_NE(error.Message.find("identity metadata"), std::string::npos)
        << error.Message;
}

TEST(ZonePackageIdentity, UnidentifiedEntitiesImportNormally)
{
    // Cook-generated content (cell meshes, lightmap entities) carries no
    // identity at all, and agreement must read that as agreement.
    const WorldComponentSchema schema = IdentityOnlySchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    (void)package.CreateEntity();

    ZoneImportError error;
    EXPECT_TRUE(ImportZonePackage(
        runtime, schema, ZoneId{ 54 }, package, LogicOnly(), &error))
        << error.Message;
    FinishResidency(runtime);
    EXPECT_EQ(AliveCount(runtime.Entities()), 1u);
}
