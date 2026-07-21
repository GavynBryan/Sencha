#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>

#include <gtest/gtest.h>

#include <vector>

struct MigrationHistory
{
    std::uint32_t FixedTicks = 0;
    float DistanceTravelled = 0.0f;
};

struct MigrationRelationship
{
    EntityId Target;
};

SENCHA_DECLARE_COMPONENT_TYPE(
    MigrationHistory,
    "test.migration_history");
SENCHA_DECLARE_COMPONENT_TYPE(
    MigrationRelationship,
    "test.migration_relationship");

namespace
{
WorldComponentSchema MigrationSchema()
{
    WorldComponentSchema schema;
    schema.Add<MigrationHistory>();
    schema.Add<MigrationRelationship>();
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
} // namespace

TEST(RuntimeWorldMigration, PlayerCrossesZoneWithoutEntityReplacement)
{
    const WorldComponentSchema schema = MigrationSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& first =
        runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    RuntimeZoneRecord& second =
        runtime.AttachZone(ZoneId{ 2 }, LiveZone());
    FinishResidency(runtime);

    const EntityId player =
        runtime.Entities().CreateEntity(first.Partition);
    runtime.Entities().AddComponent<MigrationHistory>(
        player,
        MigrationHistory{
            .FixedTicks = 240,
            .DistanceTravelled = 31.5f,
        });

    const EntityId originalIdentity = player;
    ASSERT_TRUE(runtime.MoveEntityToZone(player, ZoneId{ 2 }));

    EXPECT_EQ(player, originalIdentity);
    EXPECT_TRUE(runtime.Entities().IsAlive(originalIdentity));
    EXPECT_EQ(
        runtime.Entities().GetEntityPartition(originalIdentity),
        second.Partition);

    const MigrationHistory* history =
        runtime.Entities().TryGet<MigrationHistory>(originalIdentity);
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->FixedTicks, 240u);
    EXPECT_FLOAT_EQ(history->DistanceTravelled, 31.5f);
}

TEST(RuntimeWorldMigration, ProjectilePreservesIdentityAndComponentHistory)
{
    const WorldComponentSchema schema = MigrationSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& source =
        runtime.AttachZone(ZoneId{ 10 }, LiveZone());
    RuntimeZoneRecord& destination =
        runtime.AttachZone(ZoneId{ 20 }, LiveZone());
    FinishResidency(runtime);

    const EntityId projectile =
        runtime.Entities().CreateEntity(source.Partition);
    runtime.Entities().AddComponent<MigrationHistory>(
        projectile,
        MigrationHistory{
            .FixedTicks = 17,
            .DistanceTravelled = 8.25f,
        });

    ASSERT_TRUE(runtime.MoveEntityToZone(projectile, ZoneId{ 20 }));

    EXPECT_EQ(
        runtime.Entities().GetEntityPartition(projectile),
        destination.Partition);
    const MigrationHistory* history =
        runtime.Entities().TryGet<MigrationHistory>(projectile);
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->FixedTicks, 17u);
    EXPECT_FLOAT_EQ(history->DistanceTravelled, 8.25f);
}

TEST(RuntimeWorldMigration, CrossZoneRelationshipUsesOrdinaryEntityId)
{
    const WorldComponentSchema schema = MigrationSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& first =
        runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    RuntimeZoneRecord& second =
        runtime.AttachZone(ZoneId{ 2 }, LiveZone());
    FinishResidency(runtime);

    const EntityId owner =
        runtime.Entities().CreateEntity(first.Partition);
    const EntityId target =
        runtime.Entities().CreateEntity(second.Partition);
    runtime.Entities().AddComponent<MigrationRelationship>(
        owner,
        MigrationRelationship{ target });

    const MigrationRelationship* relationship =
        runtime.Entities().TryGet<MigrationRelationship>(owner);
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->Target, target);
    EXPECT_TRUE(runtime.Entities().IsAlive(relationship->Target));

    ASSERT_TRUE(runtime.MoveEntityToZone(target, ZoneId{ 1 }));

    relationship =
        runtime.Entities().TryGet<MigrationRelationship>(owner);
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->Target, target);
    EXPECT_TRUE(runtime.Entities().IsAlive(relationship->Target));
    EXPECT_EQ(
        runtime.Entities().GetEntityPartition(relationship->Target),
        first.Partition);
}

TEST(RuntimeWorldMigration, PublishesOneExactMigrationJournalRecord)
{
    const WorldComponentSchema schema = MigrationSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& source =
        runtime.AttachZone(ZoneId{ 5 }, LiveZone());
    RuntimeZoneRecord& destination =
        runtime.AttachZone(ZoneId{ 6 }, LiveZone());
    FinishResidency(runtime);

    const EntityId entity =
        runtime.Entities().CreateEntity(source.Partition);
    ASSERT_TRUE(runtime.MoveEntityToZone(entity, ZoneId{ 6 }));

    const std::vector<EntityPartitionMove> moves =
        runtime.Entities().ConsumePartitionMoves();
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].Entity, entity);
    EXPECT_EQ(moves[0].Previous, source.Partition);
    EXPECT_EQ(moves[0].Current, destination.Partition);
    EXPECT_TRUE(
        runtime.Entities().PendingPartitionMoves().empty());
}
