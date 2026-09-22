#pragma once

#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>

#include <bit>
#include <cstdint>
#include <span>
#include <tuple>

//=============================================================================
// AnimFacts, AnimFactsLarge, AnimFactHistory
//
// The fact snapshot: one 32-bit value per declared slot, gathered once per
// tick from gameplay and derived from other facts. Slots are addressed by the
// index the rig's bound layout gave them; the names live in the schema.
//
// Two fixed capacities, chosen per rig, as two components: an entity carries
// the one its rig asks for, or neither. A tagset slot holds nothing -- tag
// predicates read the entity's GameplayTagContainer -- and reads as 1 when the
// entity has one.
//
// AnimFactHistory is the bounded memory of the temporal derivations. Every
// closed op needs only its last transition inside its window (an edge's tick,
// when a condition last held, a smoothed value), so each derivation keeps one
// small record rather than a ring of raw samples; the observable guarantee is
// the same, since every derived fact is exact once the entity has been
// observed for one horizon, and the horizon is what ObservedSinceTick measures.
//=============================================================================

struct SENCHA_COMPONENT("sencha.anim_facts") AnimFacts
{
    std::uint32_t Values[kAnimFactsSmall] = {};
};

struct SENCHA_COMPONENT("sencha.anim_facts_large") AnimFactsLarge
{
    std::uint32_t Values[kAnimFactsLarge] = {};
};

// Encoding of each slot kind in its 32 bits. Named so a reader never has to
// remember whether a float was bit-cast or converted.
[[nodiscard]] inline std::uint32_t AnimFactFromBool(bool value) { return value ? 1u : 0u; }
[[nodiscard]] inline std::uint32_t AnimFactFromFloat(float value) { return std::bit_cast<std::uint32_t>(value); }
[[nodiscard]] inline std::uint32_t AnimFactFromInt(std::int32_t value) { return std::bit_cast<std::uint32_t>(value); }
[[nodiscard]] inline bool AnimFactToBool(std::uint32_t bits) { return bits != 0u; }
[[nodiscard]] inline float AnimFactToFloat(std::uint32_t bits) { return std::bit_cast<float>(bits); }
[[nodiscard]] inline std::int32_t AnimFactToInt(std::uint32_t bits) { return std::bit_cast<std::int32_t>(bits); }

// A numeric reading of any slot, for comparisons that accept int or float.
[[nodiscard]] float AnimFactToNumber(AnimFactKind kind, std::uint32_t bits);

// The most derivations one schema may declare. A schema needing more is
// deriving gameplay logic, which belongs in gameplay.
inline constexpr std::size_t kAnimMaxDerivations = 32;

struct AnimDerivationMemory
{
    // The last value the derivation read, and whether it has read one.
    std::uint32_t LastInput = 0;
    // The tick of the last transition the op keeps: an edge, a match, the tick
    // a condition began to hold. kAnimNoTick when there is none.
    AnimTick MarkTick = kAnimNoTick;
    // A smoothed value; a hysteresis latch as 0/1.
    float Value = 0.0f;
    bool Seen = false;
};

struct SENCHA_COMPONENT("sencha.anim_fact_history") AnimFactHistory
{
    AnimDerivationMemory Derivations[kAnimMaxDerivations] = {};
    // The tick this entity's facts were first gathered. Derived facts are exact
    // once one horizon has passed since.
    AnimTick ObservedSinceTick = kAnimNoTick;
    // The rig binding generation the memories were kept for. A rebound rig can
    // reorder or retype its derivations, so memory from an older generation is
    // discarded and the entity is observed from scratch.
    std::uint64_t BindingGeneration = 0;
};

// Every fact slot needs a place to keep derivation memory, so storage brings
// its history with it.
template <>
struct ComponentTraits<AnimFacts>
{
    using DerivedComponents = std::tuple<AnimFactHistory>;
};

template <>
struct ComponentTraits<AnimFactsLarge>
{
    using DerivedComponents = std::tuple<AnimFactHistory>;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimFacts.sencha.h>
#endif
