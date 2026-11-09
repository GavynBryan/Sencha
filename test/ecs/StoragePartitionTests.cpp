#include <ecs/Archetype.h>
#include <ecs/StoragePartitionId.h>

#include <gtest/gtest.h>

TEST(StoragePartition, RowsFromDifferentPartitionsNeverShareAChunk)
{
    ChunkBlockPool blocks;
    Archetype archetype;
    archetype.Blocks = &blocks;
    archetype.BuildLayout({});

    const StoragePartitionId first{ 1 };
    const StoragePartitionId second{ 2 };

    const auto [firstChunk, firstRow] = archetype.AddRow(10, first);
    const auto [secondChunk, secondRow] = archetype.AddRow(20, second);

    EXPECT_EQ(firstRow, 0u);
    EXPECT_EQ(secondRow, 0u);
    EXPECT_NE(firstChunk, secondChunk);
    EXPECT_EQ(archetype.Chunks[firstChunk]->Partition, first);
    EXPECT_EQ(archetype.Chunks[secondChunk]->Partition, second);
}

TEST(StoragePartition, SamePartitionReusesAvailableChunk)
{
    ChunkBlockPool blocks;
    Archetype archetype;
    archetype.Blocks = &blocks;
    archetype.BuildLayout({});

    const StoragePartitionId partition{ 7 };
    const auto [firstChunk, firstRow] = archetype.AddRow(10, partition);
    const auto [secondChunk, secondRow] = archetype.AddRow(20, partition);

    EXPECT_EQ(firstChunk, secondChunk);
    EXPECT_EQ(firstRow, 0u);
    EXPECT_EQ(secondRow, 1u);
    EXPECT_EQ(archetype.Chunks[firstChunk]->Partition, partition);
}

TEST(StoragePartition, ExistingCallersRemainInDefaultPartition)
{
    ChunkBlockPool blocks;
    Archetype archetype;
    archetype.Blocks = &blocks;
    archetype.BuildLayout({});

    const auto [chunk, row] = archetype.AddRow(10);

    EXPECT_EQ(row, 0u);
    EXPECT_EQ(archetype.Chunks[chunk]->Partition, StoragePartitionId::Default());
}

// An emptied slab is not held by the partition that emptied it — it returns to
// the archetype's free list, where any partition can claim it. Here there is only
// one claimant, so the same partition gets the same slab back at the same index.
TEST(StoragePartition, EmptiedChunkIsReclaimedAtTheSameIndex)
{
    ChunkBlockPool blocks;
    Archetype archetype;
    archetype.Blocks = &blocks;
    archetype.BuildLayout({});

    const StoragePartitionId partition{ 3 };
    const auto [chunk, row] = archetype.AddRow(10, partition);
    EXPECT_EQ(archetype.RemoveRow(chunk, row), InvalidEntityIndex);
    ASSERT_TRUE(archetype.Chunks[chunk]->IsEmpty());

    const auto [reusedChunk, reusedRow] = archetype.AddRow(11, partition);
    EXPECT_EQ(reusedChunk, chunk);
    EXPECT_EQ(reusedRow, 0u);
    EXPECT_EQ(archetype.Chunks[reusedChunk]->Partition, partition);
}
