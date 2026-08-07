#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <algorithm>
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
            {
                EXPECT_EQ(chunk->ColumnLastWrittenFrame(column), 1u);
            }
            else if (chunk->Partition == StoragePartitionId{ 2 })
            {
                EXPECT_EQ(chunk->ColumnLastWrittenFrame(column), 0u);
            }
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

// A system iterating a streamed zone can defer a creation into that zone. The
// partition comes from the chunk being iterated, so the spawn belongs to the same
// zone as the entity that asked for it rather than to the persistent partition.
//
// The created entity carries no components, so no query can observe it; what can
// be observed is the thing that matters for streaming — it is owned by the zone,
// and it dies when the zone is destroyed.
TEST_F(StoragePartitionQueryTest, DeferredCreationLandsInTheIteratedPartition)
{
    constexpr StoragePartitionId zone{ 4 };
    constexpr StoragePartitionId other{ 5 };

    Add(zone, 1);
    Add(other, 2);

    StoragePartitionSet active;
    active.Add(zone);

    const size_t before = World_.GetAliveEntities().size();

    CommandBuffer commands(World_);
    Query<Read<PartitionQueryValue>> query(World_);
    query.ForEachChunkIn(active, [&](auto& view)
    {
        commands.CreateEntity(view.Partition());
    });
    commands.Flush();

    ASSERT_EQ(World_.GetAliveEntities().size(), before + 1);

    const auto countIn = [this](StoragePartitionId partition)
    {
        size_t count = 0;
        for (const EntityId entity : World_.GetAliveEntities())
            count += World_.GetEntityPartition(entity) == partition ? 1 : 0;
        return count;
    };

    EXPECT_EQ(countIn(zone), 2u) << "the deferred creation belongs to the zone";
    EXPECT_EQ(countIn(other), 1u);
    EXPECT_EQ(countIn(StoragePartitionId::Default()), 0u)
        << "and not to the persistent partition";

    // Unloading the zone takes it with it, which is what partition ownership has
    // to mean for a streamed spawn.
    EXPECT_EQ(World_.DestroyPartition(zone), 2u);
    EXPECT_EQ(countIn(zone), 0u);
    EXPECT_EQ(countIn(other), 1u);
}

// The whole point of the handle: a system inside query scope can spawn an entity
// into the zone it is iterating AND give it components, in one recording.
TEST_F(StoragePartitionQueryTest, DeferredCreationCanBeGivenComponents)
{
    constexpr StoragePartitionId zone{ 7 };
    Add(zone, 5);

    StoragePartitionSet active;
    active.Add(zone);

    CommandBuffer commands(World_);
    Query<Read<PartitionQueryValue>> query(World_);
    query.ForEachChunkIn(active, [&](auto& view)
    {
        const auto values = view.template Read<PartitionQueryValue>();
        for (uint32_t row = 0; row < view.Count(); ++row)
        {
            const PendingEntity spawned = commands.CreateEntity(view.Partition());
            commands.AddComponent(
                spawned,
                PartitionQueryValue{ values[row].Value * 2 });
        }
    });
    commands.Flush();

    // The spawn is a full entity: it carries its component and it is in the zone,
    // so the very next query over that zone sees it.
    std::vector<int> seen;
    query.ForEachChunkIn(active, [&](auto& view)
    {
        const auto values = view.template Read<PartitionQueryValue>();
        for (uint32_t row = 0; row < view.Count(); ++row)
            seen.push_back(values[row].Value);
    });
    std::sort(seen.begin(), seen.end());
    EXPECT_EQ(seen, (std::vector<int>{ 5, 10 }));
}

// Several creations in one recording stay distinct, and each keeps the components
// addressed to it — the ordinal has to survive the flush's batching of like
// commands.
TEST_F(StoragePartitionQueryTest, ManyDeferredCreationsKeepTheirOwnComponents)
{
    constexpr StoragePartitionId zone{ 8 };

    CommandBuffer commands(World_);
    std::vector<PendingEntity> spawned;
    for (int index = 0; index < 4; ++index)
        spawned.push_back(commands.CreateEntity(zone));

    // Recorded after all four creations, so the flush groups them into one batch.
    for (int index = 0; index < 4; ++index)
        commands.AddComponent(spawned[static_cast<size_t>(index)], PartitionQueryValue{ index });

    commands.Flush();

    StoragePartitionSet active;
    active.Add(zone);

    std::vector<int> seen;
    Query<Read<PartitionQueryValue>> query(World_);
    query.ForEachChunkIn(active, [&](auto& view)
    {
        const auto values = view.template Read<PartitionQueryValue>();
        for (uint32_t row = 0; row < view.Count(); ++row)
            seen.push_back(values[row].Value);
    });
    std::sort(seen.begin(), seen.end());
    EXPECT_EQ(seen, (std::vector<int>{ 0, 1, 2, 3 }));
}

// The no-partition overload still means the persistent partition, matching
// World::CreateEntity().
TEST_F(StoragePartitionQueryTest, DeferredCreationWithoutAPartitionIsPersistent)
{
    CommandBuffer commands(World_);
    commands.CreateEntity();
    commands.Flush();

    ASSERT_EQ(World_.GetAliveEntities().size(), 1u);
    EXPECT_EQ(
        World_.GetEntityPartition(World_.GetAliveEntities()[0]),
        StoragePartitionId::Default());
}
