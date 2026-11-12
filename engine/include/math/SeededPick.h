#pragma once

#include <cstdint>

// The draw a seeded band pick compares: among the entries in a band, the one
// with the smallest key wins, so a seed picks the same entry on every run.
[[nodiscard]] std::uint64_t SeededPickKey(std::uint64_t seed, std::uint32_t order);

// Offered every entry of a band, keeps the one SeededPickKey favours.
class SeededBandPick
{
public:
    explicit SeededBandPick(std::uint64_t seed) : Seed(seed) {}

    // True when `order` now leads the band.
    bool Offer(std::uint32_t order)
    {
        const std::uint64_t key = SeededPickKey(Seed, order);
        if (Offered && key >= LeadKey)
            return false;
        Offered = true;
        LeadKey = key;
        return true;
    }

private:
    std::uint64_t Seed = 0;
    std::uint64_t LeadKey = 0;
    bool Offered = false;
};
