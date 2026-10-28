#pragma once

#include <cstdint>

// SplitMix64: advances `state` and returns a well-mixed value from it. For
// deterministic draws every machine reproduces from the same seed; not
// cryptographic.
[[nodiscard]] constexpr std::uint64_t SplitMix64(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
