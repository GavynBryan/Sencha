// Chunk slabs are reclaimed when they lose their last row, so resident chunk
// memory tracks how many chunks are live at once rather than how many have ever
// been live. The streaming-scale version of this bound lives in
// test/runtime/StreamingCostBoundsTests.cpp; what belongs here is the storage
// mechanism it depends on.

#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <vector>

struct ReclaimPosition
{
    float X = 0.0f;
    float Y = 0.0f;
};

SENCHA_DECLARE_COMPONENT_TYPE(ReclaimPosition, "test.reclaim_position");

namespace
{
class ChunkReclamationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        World_.RegisterComponent<ReclaimPosition>();
    }

    EntityId Spawn(StoragePartitionId partition, float x)
    {
        const EntityId entity = World_.CreateEntity(partition);
        World_.AddComponent<ReclaimPosition>(entity, ReclaimPosition{ x, 0.0f });
        return entity;
    }

    // The archetype holding ReclaimPosition-only rows, or null before one exists.
    const Archetype* PositionArchetype() const
    {
        const ComponentId id = World_.GetComponentId<ReclaimPosition>();
        for (const auto& archetype : World_.GetArchetypes())
        {
            if (archetype->Signature.test(id) && archetype->Columns.size() == 1)
                return archetype.get();
        }
        return nullptr;
    }

    // Counted on the owning archetype, not on the World: every CreateEntity
    // passes through the empty-signature archetype on its way to a component, so
    // a world-wide census also sees that archetype's slab.
    size_t PositionChunks() const
    {
        const Archetype* archetype = PositionArchetype();
        return archetype != nullptr ? archetype->Chunks.size() : 0;
    }

    size_t PositionEmptyChunks() const
    {
        const Archetype* archetype = PositionArchetype();
        if (archetype == nullptr)
            return 0;

        size_t empty = 0;
        for (const auto& chunk : archetype->Chunks)
            empty += chunk->IsEmpty() ? 1 : 0;
        return empty;
    }

    uint32_t RowsPerChunk(StoragePartitionId partition)
    {
        const EntityId probe = Spawn(partition, 0.0f);
        const uint32_t rows = PositionArchetype()->RowsPerChunk;
        World_.DestroyEntity(probe);
        return rows;
    }

    World World_;
};
} // namespace

TEST_F(ChunkReclamationTest, EmptiedSlabsAreReusedInsteadOfGrowingTheCensus)
{
    constexpr StoragePartitionId zone{ 1 };
    const uint32_t perChunk = RowsPerChunk(zone);

    std::vector<EntityId> entities;
    for (uint32_t index = 0; index < perChunk * 3; ++index)
        entities.push_back(Spawn(zone, static_cast<float>(index)));

    const size_t peak = PositionChunks();
    ASSERT_GE(peak, 3u);

    for (const EntityId entity : entities)
        World_.DestroyEntity(entity);
    EXPECT_EQ(PositionChunks(), peak) << "reclamation reuses slabs, it does not free them";
    EXPECT_EQ(PositionEmptyChunks(), peak);

    // The second round of the same population must fit in the same memory.
    entities.clear();
    for (uint32_t index = 0; index < perChunk * 3; ++index)
        entities.push_back(Spawn(zone, static_cast<float>(index)));

    EXPECT_EQ(PositionChunks(), peak);
    EXPECT_EQ(PositionEmptyChunks(), 0u);

    // And the rows are intact, so the reused storage is being written properly.
    for (uint32_t index = 0; index < entities.size(); ++index)
    {
        const ReclaimPosition* position =
            std::as_const(World_).TryGet<ReclaimPosition>(entities[index]);
        ASSERT_NE(position, nullptr);
        EXPECT_FLOAT_EQ(position->X, static_cast<float>(index));
    }
}

// Rows from different partitions must never share a chunk, so a reclaimed slab
// changes hands rather than being shared. The partition that emptied it must also
// lose its claim on it, or its next row would land in another partition's storage.
TEST_F(ChunkReclamationTest, AReclaimedSlabCanServeADifferentPartition)
{
    constexpr StoragePartitionId first{ 1 };
    constexpr StoragePartitionId second{ 2 };

    const EntityId original = Spawn(first, 1.0f);
    const size_t peak = PositionChunks();
    World_.DestroyEntity(original);
    ASSERT_EQ(PositionArchetype()->FreeChunkCount(), 1u);

    const EntityId adopted = Spawn(second, 2.0f);
    EXPECT_EQ(PositionChunks(), peak) << "the freed slab should have been claimed";
    EXPECT_EQ(World_.GetEntityPartition(adopted), second);
    EXPECT_EQ(PositionArchetype()->FreeChunkCount(), 0u);

    // The original partition returning needs storage of its own.
    const EntityId returning = Spawn(first, 3.0f);
    EXPECT_EQ(World_.GetEntityPartition(returning), first);
    EXPECT_GT(PositionChunks(), peak);

    EXPECT_FLOAT_EQ(std::as_const(World_).TryGet<ReclaimPosition>(adopted)->X, 2.0f);
    EXPECT_FLOAT_EQ(std::as_const(World_).TryGet<ReclaimPosition>(returning)->X, 3.0f);
}

// Releasing on the last row means a slab that still holds one row is still in use.
TEST_F(ChunkReclamationTest, ASlabKeepingOneRowIsNotReclaimed)
{
    constexpr StoragePartitionId zone{ 1 };

    const EntityId kept = Spawn(zone, 1.0f);
    const EntityId dropped = Spawn(zone, 2.0f);
    ASSERT_EQ(PositionArchetype()->FreeChunkCount(), 0u);

    World_.DestroyEntity(dropped);
    EXPECT_EQ(PositionArchetype()->FreeChunkCount(), 0u);
    EXPECT_EQ(PositionEmptyChunks(), 0u);

    World_.DestroyEntity(kept);
    EXPECT_EQ(PositionArchetype()->FreeChunkCount(), 1u);
}

// A recycled slab must not report its previous tenant's write frame, or a
// Changed<T> consumer would place the new rows in the wrong frame — too old to
// notice, or too new and never quiet. The row arriving is what stamps it.
TEST_F(ChunkReclamationTest, ARecycledSlabIsStampedWhenItsNewTenantArrives)
{
    constexpr StoragePartitionId first{ 1 };
    constexpr StoragePartitionId second{ 2 };

    World_.AdvanceFrame();
    World_.AdvanceFrame();
    World_.AdvanceFrame();
    ASSERT_EQ(World_.CurrentFrame(), 3u);

    const EntityId original = Spawn(first, 1.0f);
    const Archetype* archetype = PositionArchetype();
    ASSERT_NE(archetype, nullptr);
    ASSERT_EQ(archetype->Chunks[0]->ColumnLastWrittenFrame(0), 3u);

    World_.DestroyEntity(original);
    World_.AdvanceFrame();
    World_.AdvanceFrame();
    ASSERT_EQ(World_.CurrentFrame(), 5u);

    // By signature, so no component write can be what stamps the column: the row
    // itself has to.
    ArchetypeSignature signature;
    signature.set(World_.GetComponentId<ReclaimPosition>());
    const EntityId adopted = World_.CreateEntityWithSignature(second, signature);
    ASSERT_TRUE(World_.IsAlive(adopted));

    EXPECT_EQ(archetype->Chunks[0]->ColumnLastWrittenFrame(0), 5u)
        << "the new tenant's row must read as this frame's write, not the previous "
           "tenant's";
}
