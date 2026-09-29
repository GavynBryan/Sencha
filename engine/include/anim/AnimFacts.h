#pragma once

#include <anim/AnimSelectorState.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>

#include <bit>
#include <cstdint>
#include <span>
#include <tuple>


struct SENCHA_COMPONENT("sencha.anim_facts") AnimFacts
{
    std::uint32_t Values[kAnimFactsSmall] = {};
};

struct SENCHA_COMPONENT("sencha.anim_facts_large") AnimFactsLarge
{
    std::uint32_t Values[kAnimFactsLarge] = {};
};

// The 32-bit encoding of each slot kind; floats and ints are bit-cast.
[[nodiscard]] inline std::uint32_t AnimFactFromBool(bool value) { return value ? 1u : 0u; }
[[nodiscard]] inline std::uint32_t AnimFactFromFloat(float value) { return std::bit_cast<std::uint32_t>(value); }
[[nodiscard]] inline std::uint32_t AnimFactFromInt(std::int32_t value) { return std::bit_cast<std::uint32_t>(value); }
[[nodiscard]] inline bool AnimFactToBool(std::uint32_t bits) { return bits != 0u; }
[[nodiscard]] inline float AnimFactToFloat(std::uint32_t bits) { return std::bit_cast<float>(bits); }
[[nodiscard]] inline std::int32_t AnimFactToInt(std::uint32_t bits) { return std::bit_cast<std::int32_t>(bits); }

// A numeric reading of any slot, for comparisons that accept int or float.
[[nodiscard]] float AnimFactToNumber(AnimFactKind kind, std::uint32_t bits);

// A schema needing more is deriving gameplay logic, which belongs in gameplay.
inline constexpr std::size_t kAnimMaxDerivations = 32;

struct AnimDerivationMemory
{
    // The last value the derivation read, and whether it has read one.
    std::uint32_t LastInput = 0;
    // The tick of the last kept transition: an edge, a match, a condition starting to hold.
    AnimTick MarkTick = kAnimNoTick;
    // A smoothed value; a hysteresis latch as 0/1.
    float Value = 0.0f;
    bool Seen = false;
};

struct SENCHA_COMPONENT("sencha.anim_fact_history") AnimFactHistory
{
    AnimDerivationMemory Derivations[kAnimMaxDerivations] = {};
    // First gather; derived facts are exact once one horizon has passed since.
    AnimTick ObservedSinceTick = kAnimNoTick;
    // Memory from another generation is discarded, since a rebind can reorder or
    // retype derivations.
    std::uint64_t BindingGeneration = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimFacts.sencha.h>
#endif
