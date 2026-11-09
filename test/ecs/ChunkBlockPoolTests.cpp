// What chunk storage may do with a block: hand it to any archetype of any World
// sharing the pool, keep only a bounded number spare, and never leave a header
// pointing at one it gave up.

#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace
{
struct BlockPosition
{
    float X = 0.0f;
};

struct BlockHealth
{
    float Value = 0.0f;
};
}

SENCHA_DECLARE_COMPONENT_TYPE(BlockPosition, "test.block.position");
SENCHA_DECLARE_COMPONENT_TYPE(BlockHealth, "test.block.health");

namespace
{
void Register(World& world)
{
    world.RegisterComponent<BlockPosition>();
    world.RegisterComponent<BlockHealth>();
}

const Chunk& ChunkHolding(const World& world, ComponentId required)
{
    for (const auto& archetype : world.GetArchetypes())
        if (archetype->Signature.test(required) && !archetype->Chunks.empty())
            return *archetype->Chunks[0];
    static const Chunk none;
    return none;
}
}

TEST(ChunkBlockPool, HandsBackTheBlockItWasGiven)
{
    ChunkBlockPool pool;
    std::uint8_t* block = pool.Acquire();
    pool.Release(block);
    EXPECT_EQ(pool.Acquire(), block);
    EXPECT_EQ(pool.Stats().FromRetained, 1u);
    EXPECT_EQ(pool.Stats().FromAllocator, 1u);
    pool.Release(block);
}

TEST(ChunkBlockPool, BlocksAreAlignedForAnyColumn)
{
    ChunkBlockPool pool;
    std::uint8_t* block = pool.Acquire();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(block) % kChunkBlockAlignment, 0u);
    pool.Release(block);
}

TEST(ChunkBlockPool, KeepsTheFloorOnceNothingIsInUse)
{
    ChunkBlockPool pool;
    std::vector<std::uint8_t*> blocks;
    for (int i = 0; i < 1000; ++i)
        blocks.push_back(pool.Acquire());
    for (std::uint8_t* block : blocks)
        pool.Release(block);
    EXPECT_EQ(pool.Stats().InUse, 0u);
    EXPECT_EQ(pool.Stats().Retained, ChunkBlockPool::kMinRetainedBlocks);
    EXPECT_EQ(pool.Stats().ToAllocator, 1000u - ChunkBlockPool::kMinRetainedBlocks);
}

TEST(ChunkBlockPool, KeepsAShareOfWhatIsInUse)
{
    ChunkBlockPool pool;
    std::vector<std::uint8_t*> blocks;
    for (int i = 0; i < 2000; ++i)
        blocks.push_back(pool.Acquire());
    for (int i = 0; i < 1200; ++i)
    {
        pool.Release(blocks.back());
        blocks.pop_back();
    }
    EXPECT_EQ(pool.Stats().InUse, 800u);
    EXPECT_EQ(pool.Stats().Retained, 800u / ChunkBlockPool::kRetainedShareOfInUse);
    for (std::uint8_t* block : blocks)
        pool.Release(block);
}

TEST(ChunkBlocks, AChunkThatLosesItsLastRowHoldsNoBlock)
{
    World world;
    Register(world);
    const EntityId entity = world.CreateEntity();
    world.AddComponent<BlockPosition>(entity, BlockPosition{ 1.0f });
    const ComponentId position = world.GetComponentId<BlockPosition>();
    EXPECT_NE(ChunkHolding(world, position).Data, nullptr);

    const std::size_t inUse = world.ChunkBlockStats().InUse;
    world.DestroyEntity(entity);
    EXPECT_EQ(ChunkHolding(world, position).Data, nullptr);
    EXPECT_EQ(world.ChunkBlockStats().InUse, inUse - 1);
}

TEST(ChunkBlocks, OneArchetypesBlockServesAnother)
{
    World world;
    Register(world);
    const EntityId first = world.CreateEntity();
    world.AddComponent<BlockPosition>(first, BlockPosition{ 1.0f });
    world.DestroyEntity(first);
    const std::uint64_t allocated = world.ChunkBlockStats().FromAllocator;

    const EntityId second = world.CreateEntity();
    world.AddComponent<BlockHealth>(second, BlockHealth{ 7.0f });
    EXPECT_EQ(world.ChunkBlockStats().FromAllocator, allocated);
    EXPECT_FLOAT_EQ(std::as_const(world).TryGet<BlockHealth>(second)->Value, 7.0f);
}

TEST(ChunkBlocks, WorldsSharingAPoolReuseEachOthersBlocks)
{
    auto pool = std::make_shared<ChunkBlockPool>();
    World first(pool);
    World second(pool);
    Register(first);
    Register(second);

    const EntityId gone = first.CreateEntity();
    first.AddComponent<BlockPosition>(gone, BlockPosition{ 1.0f });
    first.DestroyEntity(gone);
    const std::uint64_t allocated = pool->Stats().FromAllocator;

    const EntityId arrived = second.CreateEntity();
    second.AddComponent<BlockPosition>(arrived, BlockPosition{ 2.0f });
    EXPECT_EQ(pool->Stats().FromAllocator, allocated);
    EXPECT_FLOAT_EQ(std::as_const(second).TryGet<BlockPosition>(arrived)->X, 2.0f);
}

TEST(ChunkBlocks, ADestroyedWorldReturnsEveryBlock)
{
    auto pool = std::make_shared<ChunkBlockPool>();
    {
        World world(pool);
        Register(world);
        for (int i = 0; i < 16; ++i)
            world.AddComponent<BlockPosition>(world.CreateEntity(), BlockPosition{});
        EXPECT_GT(pool->Stats().InUse, 0u);
    }
    EXPECT_EQ(pool->Stats().InUse, 0u);
}

TEST(ChunkBlocks, AMigratedRowKeepsItsValuesAsItsOldBlockIsReleased)
{
    World world;
    Register(world);
    const EntityId entity = world.CreateEntity();
    world.AddComponent<BlockPosition>(entity, BlockPosition{ 3.0f });
    world.AddComponent<BlockHealth>(entity, BlockHealth{ 9.0f });
    world.CreateEntity();
    EXPECT_FLOAT_EQ(std::as_const(world).TryGet<BlockPosition>(entity)->X, 3.0f);
    EXPECT_FLOAT_EQ(std::as_const(world).TryGet<BlockHealth>(entity)->Value, 9.0f);
}

TEST(ChunkBlocks, AMovedWorldKeepsItsRowsAndItsPool)
{
    World source;
    Register(source);
    const EntityId entity = source.CreateEntity();
    source.AddComponent<BlockPosition>(entity, BlockPosition{ 5.0f });

    World moved(std::move(source));
    EXPECT_FLOAT_EQ(std::as_const(moved).TryGet<BlockPosition>(entity)->X, 5.0f);

    const std::size_t inUse = moved.ChunkBlockStats().InUse;
    const EntityId fresh = moved.CreateEntity();
    moved.AddComponent<BlockHealth>(fresh, BlockHealth{ 6.0f });
    EXPECT_GT(moved.ChunkBlockStats().InUse, inUse);
    EXPECT_FLOAT_EQ(std::as_const(moved).TryGet<BlockHealth>(fresh)->Value, 6.0f);
}
