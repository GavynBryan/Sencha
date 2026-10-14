#pragma once

#include <cstdint>
#include <random>
#include <string>

// Random 64-bit values for identities an editor mints: unpredictable across
// machines and branches, so two people adding elements never collide on merge.
class RandomKeySource
{
public:
    [[nodiscard]] std::uint64_t Next() { return Engine(); }
    // From now on the keys follow from `seed`, so a replay draws the same ones.
    void Seed(std::uint64_t seed) { Engine.seed(seed); }

private:
    std::mt19937_64 Engine{ std::random_device{}() };
};

// A fresh StableKey value: sixteen lowercase hex digits.
[[nodiscard]] std::string MintStableKey();
