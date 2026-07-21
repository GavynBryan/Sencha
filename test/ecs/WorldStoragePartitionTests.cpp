#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{
struct PartitionPosition
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct PartitionVelocity
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct PartitionTracked
{
    int Value = 0;
};

int gPartitionRemoveCount = 0;

SENCHA_DECLARE_COMPONENT_TYPE(PartitionPosition, "test.partition_position");
SENCHA_DECLARE_COMPONENT_TYPE(PartitionVelocity, "test.partition_velocity");
SENCHA_DECLARE_COMPONENT_TYPE(PartitionTracked, "test.partition_tracked");
} // namespace

template <>
struct ComponentTraits<PartitionTracked>
{
    static void OnRemove(
        const PartitionTracked&,
        World&,
        EntityId)
    {
        ++gPartitionRemoveCount;
    }
};

namespace
{
class WorldStoragePartitionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        gPartitionRemoveCount = 0;
        World_.RegisterComponent<PartitionPosition>();
        World_.RegisterComponent<PartitionVelocity>();
        World_.RegisterComponent<PartitionTracked>();
    }

    World World_;
};
} // namespace

TEST_F(WorldStoragePartitionTest, ExistingCreationUsesDefaultPartition)
{
    const EntityId entity = World_.CreateEntity();
    EXPECT_EQ(
        World_.GetEntityPartition(entity),
        StoragePartitionId::Default());
}

TEST_F(WorldStoragePartitionTest, ExplicitCreationUsesRequestedPartition)
{
    const StoragePartitionId partition{ 7 };
    const EntityId entity = World_.CreateEntity(partition);

    EXPECT_EQ(World_.GetEntityPartition(entity), partition);
    EXPECT_EQ(World_.LocateEntity(entity).Partition, partition);
    ASSERT_NE(World_.LocateEntity(entity).ChunkPtr, nullptr);
    EXPECT_EQ(World_.LocateEntity(entity).ChunkPtr->Partition, partition);
}

TEST_F(WorldStoragePartitionTest, StructuralMutationPreservesPartition)
{
    const StoragePartitionId partition{ 3 };
    const EntityId entity = World_.CreateEntity(partition);

    World_.AddComponent<PartitionPosition>(entity, { 4.0f, 5.0f });
    EXPECT_EQ(World_.GetEntityPartition(entity), partition);
    EXPECT_EQ(World_.LocateEntity(entity).ChunkPtr->Partition, partition);

    World_.AddComponent<PartitionVelocity>(entity, { 1.0f, 2.0f });
    EXPECT_EQ(World_.GetEntityPartition(entity), partition);

    World_.RemoveComponent<PartitionVelocity>(entity);
    EXPECT_EQ(World_.GetEntityPartition(entity), partition);
    EXPECT_FLOAT_EQ(
        std::as_const(World_).TryGet<PartitionPosition>(entity)->X,
        4.0f);
}

TEST_F(WorldStoragePartitionTest, CommandBufferMutationPreservesPartition)
{
    const StoragePartitionId partition{ 11 };
    const EntityId entity = World_.CreateEntity(partition);

    CommandBuffer commands(World_);
    commands.AddComponent<PartitionPosition>(entity, { 8.0f, 9.0f });
    commands.AddComponent<PartitionVelocity>(entity, { 2.0f, 3.0f });
    commands.Flush();

    EXPECT_EQ(World_.GetEntityPartition(entity), partition);
    EXPECT_TRUE(World_.HasComponent<PartitionPosition>(entity));
    EXPECT_TRUE(World_.HasComponent<PartitionVelocity>(entity));

    commands.RemoveComponent<PartitionVelocity>(entity);
    commands.Flush();

    EXPECT_EQ(World_.GetEntityPartition(entity), partition);
    EXPECT_FALSE(World_.HasComponent<PartitionVelocity>(entity));
}

TEST_F(WorldStoragePartitionTest, MigrationPreservesIdentityAndComponentValues)
{
    const StoragePartitionId source{ 1 };
    const StoragePartitionId destination{ 2 };
    const EntityId entity = World_.CreateEntity(source);
    World_.AddComponent<PartitionPosition>(entity, { 12.0f, 13.0f });
    World_.AddComponent<PartitionVelocity>(entity, { -2.0f, 6.0f });

    ASSERT_TRUE(World_.MoveEntityToPartition(entity, destination));

    EXPECT_TRUE(World_.IsAlive(entity));
    EXPECT_EQ(World_.GetEntityPartition(entity), destination);
    EXPECT_EQ(World_.LocateEntity(entity).ChunkPtr->Partition, destination);

    const World& readOnly = World_;
    const PartitionPosition* position =
        readOnly.TryGet<PartitionPosition>(entity);
    const PartitionVelocity* velocity =
        readOnly.TryGet<PartitionVelocity>(entity);
    ASSERT_NE(position, nullptr);
    ASSERT_NE(velocity, nullptr);
    EXPECT_FLOAT_EQ(position->X, 12.0f);
    EXPECT_FLOAT_EQ(position->Y, 13.0f);
    EXPECT_FLOAT_EQ(velocity->X, -2.0f);
    EXPECT_FLOAT_EQ(velocity->Y, 6.0f);
}

TEST_F(WorldStoragePartitionTest, MigrationBumpsBothPartitionVersionsOnce)
{
    const StoragePartitionId source{ 4 };
    const StoragePartitionId destination{ 9 };
    const EntityId entity = World_.CreateEntity(source);
    World_.AddComponent<PartitionPosition>(entity, {});

    const uint64_t globalBefore = World_.StructuralVersion();
    const uint64_t sourceBefore = World_.StructuralVersion(source);
    const uint64_t destinationBefore = World_.StructuralVersion(destination);

    ASSERT_TRUE(World_.MoveEntityToPartition(entity, destination));

    EXPECT_EQ(World_.StructuralVersion(), globalBefore + 1);
    EXPECT_EQ(World_.StructuralVersion(source), sourceBefore + 1);
    EXPECT_EQ(
        World_.StructuralVersion(destination),
        destinationBefore + 1);
}

TEST_F(WorldStoragePartitionTest, NoOpMigrationChangesNothing)
{
    const StoragePartitionId partition{ 5 };
    const EntityId entity = World_.CreateEntity(partition);
    const uint64_t globalBefore = World_.StructuralVersion();
    const uint64_t partitionBefore = World_.StructuralVersion(partition);

    EXPECT_FALSE(World_.MoveEntityToPartition(entity, partition));
    EXPECT_EQ(World_.StructuralVersion(), globalBefore);
    EXPECT_EQ(World_.StructuralVersion(partition), partitionBefore);
    EXPECT_TRUE(World_.PendingPartitionMoves().empty());
}

TEST_F(WorldStoragePartitionTest, MigrationJournalPublishesAndConsumesMove)
{
    const StoragePartitionId source{ 2 };
    const StoragePartitionId destination{ 6 };
    const EntityId entity = World_.CreateEntity(source);

    ASSERT_TRUE(World_.MoveEntityToPartition(entity, destination));

    const std::span<const EntityPartitionMove> pending =
        World_.PendingPartitionMoves();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].Entity, entity);
    EXPECT_EQ(pending[0].Previous, source);
    EXPECT_EQ(pending[0].Current, destination);

    const std::vector<EntityPartitionMove> consumed =
        World_.ConsumePartitionMoves();
    ASSERT_EQ(consumed.size(), 1u);
    EXPECT_EQ(consumed[0].Entity, entity);
    EXPECT_TRUE(World_.PendingPartitionMoves().empty());
}

TEST_F(WorldStoragePartitionTest, MigrationMarksDestinationColumnsChanged)
{
    const StoragePartitionId source{ 1 };
    const StoragePartitionId destination{ 2 };
    const EntityId entity = World_.CreateEntity(source);
    World_.AddComponent<PartitionPosition>(entity, { 2.0f, 3.0f });

    World_.AdvanceFrame();
    ASSERT_EQ(World_.CurrentFrame(), 1u);
    ASSERT_TRUE(World_.MoveEntityToPartition(entity, destination));

    std::vector<EntityId> changed;
    Query<Read<PartitionPosition>, Changed<PartitionPosition>> query(World_);
    query.ForEachChunk(
        [&](auto& view)
        {
            for (uint32_t row = 0; row < view.Count(); ++row)
            {
                const EntityIndex index = view.Entities()[row];
                if (index == entity.Index)
                    changed.push_back(entity);
            }
        },
        0);

    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], entity);
}

TEST_F(WorldStoragePartitionTest, DestroyPartitionOnlyDestroysOwnedEntities)
{
    const StoragePartitionId first{ 1 };
    const StoragePartitionId second{ 2 };

    const EntityId firstA = World_.CreateEntity(first);
    const EntityId firstB = World_.CreateEntity(first);
    const EntityId secondA = World_.CreateEntity(second);
    World_.AddComponent<PartitionTracked>(firstA, { 1 });
    World_.AddComponent<PartitionTracked>(firstB, { 2 });
    World_.AddComponent<PartitionTracked>(secondA, { 3 });

    EXPECT_EQ(World_.DestroyPartition(first), 2u);

    EXPECT_FALSE(World_.IsAlive(firstA));
    EXPECT_FALSE(World_.IsAlive(firstB));
    EXPECT_TRUE(World_.IsAlive(secondA));
    EXPECT_EQ(World_.GetEntityPartition(secondA), second);
    EXPECT_EQ(gPartitionRemoveCount, 2);
}
