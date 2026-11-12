#include <math/SeededPick.h>

#include <core/hash/ContentHash.h>

#include <array>
#include <bit>
#include <cstddef>

std::uint64_t SeededPickKey(std::uint64_t seed, std::uint32_t order)
{
    const std::array<std::byte, sizeof(order)> bytes = std::bit_cast<std::array<std::byte, sizeof(order)>>(order);
    return HashBytes64(bytes, seed);
}
