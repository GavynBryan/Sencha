#pragma once

#include <algorithm>
#include <cstddef>

// How many freed items a pool keeps for reuse: the larger of Floor and a
// ShareOfLive-th of those in use. The rest go back to the allocator, so what a
// pool holds follows the live population, and a warmed-up one allocates nothing.
struct SpareRetention
{
    std::size_t Floor = 64;
    std::size_t ShareOfLive = 4;

    [[nodiscard]] constexpr std::size_t Limit(std::size_t live) const { return std::max(Floor, live / ShareOfLive); }
};
