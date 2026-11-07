#pragma once

#include <cstdint>

// The draw a seeded band pick compares: among the entries in a band, the one
// with the smallest key wins, so a seed picks the same entry on every run.
[[nodiscard]] std::uint64_t SeededPickKey(std::uint64_t seed, std::uint32_t order);
