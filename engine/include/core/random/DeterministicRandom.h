#pragma once

#include <cstdint>

//=============================================================================
// DeterministicRandom (docs/plans/t-language-v1-spec.md, section D.6)
//
// PCG32 seeded through a SplitMix64 mix, for gameplay that must produce the
// same stream on every machine and every run. Not cryptographic.
//
// A stream is a pure function of its seed inputs (world seed, a stream id,
// and the fixed tick index), so replay needs no stored RNG state: re-seed
// per callback from the same inputs and the draws repeat. This is the
// engine primitive the T host's ctx.random binds to; it is useful anywhere
// gameplay needs reproducible randomness.
//=============================================================================

// SplitMix64: mixes an arbitrary 64-bit value into a well-distributed one.
// Used both to fold the seed inputs together and to derive PCG's initial
// state and sequence from a single seed.
[[nodiscard]] constexpr uint64_t SplitMix64(uint64_t value)
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

// Folds an ordered set of inputs into one seed. Order matters: (a, b) and
// (b, a) yield different seeds.
[[nodiscard]] constexpr uint64_t MixSeed(uint64_t a, uint64_t b)
{
    return SplitMix64(SplitMix64(a) ^ (b + 0x9E3779B97F4A7C15ull));
}

class DeterministicRandom
{
public:
    DeterministicRandom() = default;

    // Seeds directly from a mixed 64-bit value.
    explicit constexpr DeterministicRandom(uint64_t seed)
    {
        State = 0;
        Increment = (SplitMix64(seed ^ 0xDA3E39CB94B95BDBull) << 1u) | 1u; // must be odd
        Step();
        State += SplitMix64(seed);
        Step();
    }

    // Seeds from ordered inputs, the intended entry point: for a script
    // callback, (world seed, stream id, tick index) where stream id folds
    // the subject entity and the declaration name.
    [[nodiscard]] static constexpr DeterministicRandom FromInputs(uint64_t worldSeed,
                                                                  uint64_t streamId,
                                                                  uint64_t tickIndex)
    {
        return DeterministicRandom(MixSeed(MixSeed(worldSeed, streamId), tickIndex));
    }

    // Next 32-bit value (PCG-XSH-RR).
    [[nodiscard]] constexpr uint32_t NextU32()
    {
        const uint64_t previous = State;
        Step();
        const uint32_t xorshifted =
            static_cast<uint32_t>(((previous >> 18u) ^ previous) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(previous >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform float in [0, 1). 24 bits of mantissa, so evenly spaced.
    [[nodiscard]] constexpr float NextFloat01()
    {
        return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f);
    }

    // Uniform integer in [lo, hi] inclusive. lo == hi returns lo; lo > hi is
    // the caller's contract to avoid (the T host traps on it).
    [[nodiscard]] constexpr int32_t NextRange(int32_t lo, int32_t hi)
    {
        if (lo >= hi)
        {
            return lo;
        }
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        // Rejection sampling removes modulo bias, keeping the result a pure
        // function of the draw sequence.
        const uint32_t limit = span == 0u ? 0u : (0u - span) % span;
        uint32_t draw = NextU32();
        while (draw < limit)
        {
            draw = NextU32();
        }
        return lo + static_cast<int32_t>(draw % span);
    }

private:
    constexpr void Step()
    {
        State = State * 0x5851F42D4C957F2Dull + Increment;
    }

    uint64_t State = 0x853C49E6748FEA9Bull;
    uint64_t Increment = 0xDA3E39CB94B95BDBull;
};
