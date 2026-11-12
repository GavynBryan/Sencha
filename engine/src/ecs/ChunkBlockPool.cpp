#include <ecs/ChunkBlockPool.h>

#include <ecs/Chunk.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>

namespace
{
constexpr std::align_val_t kBlockAlignment{ kChunkBlockAlignment };

std::uint8_t* NewBlock()
{
    return static_cast<std::uint8_t*>(::operator new(ChunkSizeBytes, kBlockAlignment));
}

void DeleteBlock(std::uint8_t* block) noexcept
{
    ::operator delete(block, ChunkSizeBytes, kBlockAlignment);
}
}

ChunkBlockPool::~ChunkBlockPool()
{
    assert(Counts.InUse == 0 && "a chunk outlived the pool its block came from");
    for (std::uint8_t* block : Retained)
        DeleteBlock(block);
}

std::uint8_t* ChunkBlockPool::Acquire()
{
    std::uint8_t* block = nullptr;
    {
        const std::lock_guard guard(Lock);
        if (!Retained.empty())
        {
            block = Retained.back();
            Retained.pop_back();
            Counts.Retained = Retained.size();
            ++Counts.FromRetained;
        }
        else
        {
            ++Counts.FromAllocator;
        }
        ++Counts.InUse;
        Counts.PeakInUse = std::max(Counts.PeakInUse, Counts.InUse);
    }
    if (block == nullptr)
        block = NewBlock();
#ifndef NDEBUG
    std::memset(block, 0xCD, ChunkSizeBytes);
#endif
    return block;
}

void ChunkBlockPool::Release(std::uint8_t* block) noexcept
{
    assert(block != nullptr);
#ifndef NDEBUG
    std::memset(block, 0xDD, ChunkSizeBytes);
#endif
    std::vector<std::uint8_t*> surplus;
    {
        const std::lock_guard guard(Lock);
        assert(Counts.InUse > 0);
        --Counts.InUse;
        Retained.push_back(block);
        const std::size_t limit = kRetention.Limit(Counts.InUse);
        if (Retained.size() > limit)
        {
            surplus.assign(Retained.begin() + static_cast<std::ptrdiff_t>(limit), Retained.end());
            Retained.resize(limit);
            Counts.ToAllocator += surplus.size();
        }
        Counts.Retained = Retained.size();
    }
    for (std::uint8_t* extra : surplus)
        DeleteBlock(extra);
}

ChunkBlockPool::Counters ChunkBlockPool::Stats() const
{
    const std::lock_guard guard(Lock);
    return Counts;
}
