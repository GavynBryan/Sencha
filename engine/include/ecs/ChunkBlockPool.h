#pragma once

#include <core/containers/SpareRetention.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// Where chunk column storage comes from. Every block has one size and one
// alignment, so a block one archetype gave up serves any other, in any World
// sharing this pool. Thread-safe: a World is built off the main thread while
// another runs.
//
// A block comes back as its last tenant left it. A row's columns are written by
// whoever placed them, and debug builds scribble over every block handed out or
// taken back, so a reader relying on stale bytes or a released block fails.
class ChunkBlockPool
{
public:
    static constexpr SpareRetention kRetention{};

    struct Counters
    {
        std::size_t InUse = 0;
        std::size_t Retained = 0;
        std::size_t PeakInUse = 0;
        std::uint64_t FromAllocator = 0;
        std::uint64_t FromRetained = 0;
        std::uint64_t ToAllocator = 0;
    };

    ChunkBlockPool() = default;
    ~ChunkBlockPool();

    ChunkBlockPool(const ChunkBlockPool&) = delete;
    ChunkBlockPool& operator=(const ChunkBlockPool&) = delete;

    [[nodiscard]] std::uint8_t* Acquire();
    void Release(std::uint8_t* block) noexcept;

    [[nodiscard]] Counters Stats() const;

private:
    mutable std::mutex Lock;
    std::vector<std::uint8_t*> Retained;
    Counters Counts;
};
