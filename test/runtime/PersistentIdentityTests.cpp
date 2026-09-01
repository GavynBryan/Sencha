#include <ecs/WorldComponentSchema.h>
#include <world/WorldComponentSchemas.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

namespace
{
WorldComponentSchema IdentitySchema()
{
    WorldComponentSchema schema;
    schema.Add<PersistentIdComponent>();
    schema.Seal();
    return schema;
}

ZoneParticipation LiveZone()
{
    return ZoneParticipation{
        .Visible = true,
        .Physics = true,
        .Logic = true,
        .Audio = true,
    };
}

void FinishResidency(RuntimeWorld& runtime)
{
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();
}

PersistentEntityIndex& IndexOf(RuntimeWorld& runtime)
{
    auto* index = runtime.Entities().TryGetResource<PersistentEntityIndex>();
    EXPECT_NE(index, nullptr);
    return *index;
}
} // namespace

TEST(PersistentIdentity, RegistersOnAddAndResolves)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x1111 };
    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, { id });

    EXPECT_EQ(IndexOf(runtime).TryResolve(id), entity);
    EXPECT_EQ(IndexOf(runtime).Count(), 1u);
}

TEST(PersistentIdentity, UnregistersOnEntityDestroy)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x2222 };
    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, { id });
    runtime.Entities().DestroyEntity(entity);

    EXPECT_FALSE(IndexOf(runtime).TryResolve(id).IsValid());
    EXPECT_EQ(IndexOf(runtime).Count(), 0u);
}

TEST(PersistentIdentity, RemoveComponentUnregisters)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x3333 };
    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, { id });
    runtime.Entities().RemoveComponent<PersistentIdComponent>(entity);

    EXPECT_FALSE(IndexOf(runtime).TryResolve(id).IsValid());
    EXPECT_TRUE(runtime.Entities().IsAlive(entity));
}

TEST(PersistentIdentity, UnsetIdStaysOutOfIndex)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, {});

    EXPECT_EQ(IndexOf(runtime).Count(), 0u);
    runtime.Entities().DestroyEntity(entity);
}

TEST(PersistentIdentity, FirstRegistrationWinsOnCollision)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x4444 };
    const EntityId first = runtime.Entities().CreateEntity(zone.Partition);
    const EntityId second = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(first, { id });
    runtime.Entities().AddComponent<PersistentIdComponent>(second, { id });

    EXPECT_EQ(IndexOf(runtime).TryResolve(id), first);
    EXPECT_EQ(IndexOf(runtime).CollisionCount(), 1u);

    // Destroying the collision loser must not evict the winner's entry.
    runtime.Entities().DestroyEntity(second);
    EXPECT_EQ(IndexOf(runtime).TryResolve(id), first);

    runtime.Entities().DestroyEntity(first);
    EXPECT_FALSE(IndexOf(runtime).TryResolve(id).IsValid());
}

TEST(PersistentIdentity, ZoneDetachClearsExactlyItsEntries)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& first = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    RuntimeZoneRecord& second = runtime.AttachZone(ZoneId{ 2 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId inFirst{ 0x5551 };
    const PersistentEntityId inSecond{ 0x5552 };
    const PersistentEntityId inPersistent{ 0x5553 };

    const EntityId firstEntity = runtime.Entities().CreateEntity(first.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(firstEntity, { inFirst });
    const EntityId secondEntity = runtime.Entities().CreateEntity(second.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(secondEntity, { inSecond });
    const EntityId persistentEntity =
        runtime.Entities().CreateEntity(PersistentStoragePartition);
    runtime.Entities().AddComponent<PersistentIdComponent>(
        persistentEntity, { inPersistent });

    ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
    runtime.FlushLifecycleRequests();
    FinishResidency(runtime);

    EXPECT_FALSE(IndexOf(runtime).TryResolve(inFirst).IsValid());
    EXPECT_EQ(IndexOf(runtime).TryResolve(inSecond), secondEntity);
    EXPECT_EQ(IndexOf(runtime).TryResolve(inPersistent), persistentEntity);
    EXPECT_EQ(IndexOf(runtime).Count(), 2u);
}
