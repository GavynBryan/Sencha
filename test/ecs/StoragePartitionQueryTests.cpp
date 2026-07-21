#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

struct PartitionQueryValue
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(PartitionQueryValue, "test.partition_query_value");

namespace
{
class StoragePartitionQueryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        World_.RegisterComponent<PartitionQueryValue>();
    }

    EntityId Add(StoragePartitionId partition, int value)
    {
        const EntityId entity = World_.CreateEntity(partition);
        World_.AddComponent<PartitionQueryValue>(
            entity,
            PartitionQueryValue{ value });
        return entity;
    }

    World World_;
};
} // namespace

TEST(StoragePartitionSet, MembershipAndIterationAreDeterministic)
{
    StoragePartitionSet partitions;
    EXPECT_TRUE(partitions.Empty());

    EXPECT_TRUE(partitions.Add(StoragePartitionId{ 9 }));
    EXPECT_TRUE(partitions.Add(StoragePartitionId{ 2 }));
    EXPECT_FALSE(partitions.Add(StoragePartitionId{ 9 }));

    EXPECT_TRUE(partitions.Contains(StoragePartitionId{ 9 }));
    EXPECT_TRUE(partitions.Contains(StoragePartitionId{ 2 }));
    EXPECT_FALSE(partitions.Contains(StoragePartitionId{ 3 }));
    ASSERT_EQ(partitions.Size(), 2u);
    ASSERT_EQ(partitions.Members().size(), 2u);
    EXPECT_EQ(partitions.Members()[0], StoragePartitionId{ 9 });
    EXPECT_EQ(partitions.Members()[1], StoragePartitionId{ 2 });

    EXPECT_TRUE(partitions.Remove(StoragePartitionId{ 9 }));
    EXPECT_FALSE(partitions.Remove(StoragePartitionId{ 9 }));
    ASSERT_EQ(partitions.Members().size(), 1u);
    EXPECT_EQ(partitions.Members()[0], StoragePartitionId{ 2 });

    partitions.Clear();
    EXPECT_TRUE(partitions.Empty());
    EXPECT_FALSE(partitions.Contains(StoragePartitionId{ 2 }));
}

TEST_F(StoragePartitionQueryTest, UnfilteredPathStillVisitsEveryPartition)
{
    Add(StoragePartitionId::Default(), 1);
    Add(StoragePartitionId{ 1 }, 2);
    Add(StoragePartitionId{ 2 }, 3);

    Query<Read<PartitionQueryValue>> query(World_);
    int count = 0;
    int sum = 0;
    query.ForEachChunk(
        [&](auto& view)
        {
            const auto values = view.template Read<PartitionQueryValue>();
            count += static_cast<int>(view.Count());
            for (const PartitionQueryValue value : values)
                sum += value.Value;
        });

    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 6);
}

TEST_F(StoragePartitionQueryTest, FilteredPathVisitsOnlySelectedPartitions)
{
    Add(StoragePartitionId::Default(), 1);
    Add(StoragePartitionId{ 1 }, 10);
    Add(StoragePartitionId{ 2 }, 100);

    StoragePartitionSet active;
    active.Add(StoragePartitionId{ 2 });

    Query<Read<PartitionQueryValue>> query(World_);
    int count = 0;
    int sum = 0;
    std::vector<StoragePartitionId> visited;
    query.ForEachChunkIn(
        active,
        [&](auto& view)
        {
            visited.push_back(view.Partition());
            const auto values = view.template Read<PartitionQueryValue>();
            count += static_cast<int>(view.Count());
            for (const PartitionQueryValue value : values)
                sum += value.Value;
        });

    EXPECT_EQ(count, 1);
    EXPECT_EQ(sum, 100);
    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], StoragePartitionId{ 2 });
}

TEST_F(StoragePartitionQueryTest, EmptyPartitionSetVisitsNothing)
{
    Add(StoragePartitionId{ 1 }, 1);
    Add(StoragePartitionId{ 2 }, 2);

    StoragePartitionSet active;
    Query<Read<PartitionQueryValue>> query(World_);
    int count = 0;
    query.ForEachChunkIn(
        active,
        [&](auto& view)
        {
            count += static_cast<int>(view.Count());
        });

    EXPECT_EQ(count, 0);
}

TEST_F(StoragePartitionQueryTest, OneCachedQueryCanServeDifferentDomainSets)
{
    Add(StoragePartitionId{ 1 }, 10);
    Add(StoragePartitionId{ 2 }, 20);

    Query<Read<PartitionQueryValue>> query(World_);
    StoragePartitionSet active;

    active.Add(StoragePartitionId{ 1 });
    int first = 0;
    query.ForEachChunkIn(
        active,
        [&](auto& view)
        {
            for (const PartitionQueryValue value :
                 view.template Read<PartitionQueryValue>())
            {
                first += value.Value;
            }
        });

    active.Clear();
    active.Add(StoragePartitionId{ 2 });
    int second = 0;
    query.ForEachChunkIn(
        active,
        [&](auto& view)
        {
            for (const PartitionQueryValue value :
                 view.template Read<PartitionQueryValue>())
            {
                second += value.Value;
            }
        });

    EXPECT_EQ(first, 10);
    EXPECT_EQ(second, 20);
}

TEST_F(StoragePartitionQueryTest, WriteVersionsBumpOnlyVisitedPartitions)
{
    Add(StoragePartitionId{ 1 }, 10);
    Add(StoragePartitionId{ 2 }, 20);
    World_.AdvanceFrame();

    StoragePartitionSet active;
    active.Add(StoragePartitionId{ 1 });

    Query<Write<PartitionQueryValue>> query(World_);
    query.ForEachChunkIn(
        active,
        [](auto&) {
            // Conservative Write<T> semantics bump after the callback even when
            // no row is modified.
        });

    const ComponentId valueId = World_.GetComponentId<PartitionQueryValue>();
    for (const auto& archetype : World_.GetArchetypes())
    {
        if (!archetype->Signature.test(valueId))
            continue;

        for (const auto& chunk : archetype->Chunks)
        {
            if (chunk->IsEmpty())
                continue;
            const uint32_t column = chunk->FindColumn(valueId);
            ASSERT_NE(column, UINT32_MAX);
            if (chunk->Partition == StoragePartitionId{ 1 })
                EXPECT_EQ(chunk->ColumnLastWrittenFrame(column), 1u);
            else if (chunk->Partition == StoragePartitionId{ 2 })
                EXPECT_EQ(chunk->ColumnLastWrittenFrame(column), 0u);
        }
    }
}

TEST_F(StoragePartitionQueryTest, ChangedAndPartitionFiltersCompose)
{
    const EntityId first = Add(StoragePartitionId{ 1 }, 10);
    const EntityId second = Add(StoragePartitionId{ 2 }, 20);
    (void)first;

    World_.AdvanceFrame();
    PartitionQueryValue* changed = World_.TryGet<PartitionQueryValue>(second);
    ASSERT_NE(changed, nullptr);
    changed->Value = 21;

    Query<Read<PartitionQueryValue>, Changed<PartitionQueryValue>> query(World_);
    StoragePartitionSet active;
    active.Add(StoragePartitionId{ 1 });

    int count = 0;
    query.ForEachChunkIn(
        active,
        [&](auto& view)
        {
            count += static_cast<int>(view.Count());
        },
        0);
    EXPECT_EQ(count, 0);

    active.Clear();
    active.Add(StoragePartitionId{ 2 });
    query.ForEachChunkIn(
        active,
        [&](auto& view)
        {
            count += static_cast<int>(view.Count());
        },
        0);
    EXPECT_EQ(count, 1);
}
