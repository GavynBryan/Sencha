#pragma once

#include <cstddef>

// Starting points measured against the first content, not product limits. Every
// one is part of a component layout or a compiled table, so changing one is an ABI
// change; the compiler reports content that exceeds one as `capacity`.

inline constexpr std::size_t kAbilityGrantCapacity = 16;
inline constexpr std::size_t kAbilityLiveRecordCapacity = 4;
inline constexpr std::size_t kAbilityLingeringRecordCapacity = 2;
// The authority's records: the slots every machine is sent.
inline constexpr std::size_t kAbilityRecordCapacity = kAbilityLiveRecordCapacity + kAbilityLingeringRecordCapacity;
// A predicting client's guesses, in slots after the authority's: one for every
// activation the authority can hold live, so a client can guess whatever it admits.
inline constexpr std::size_t kAbilityGuessCapacity = kAbilityLiveRecordCapacity;
inline constexpr std::size_t kAbilitySlotCapacity = kAbilityRecordCapacity + kAbilityGuessCapacity;

// Slots, not locals: a hit or an entity list takes two.
inline constexpr std::size_t kAbilityLocalSlotCapacity = 8;
inline constexpr std::size_t kAbilityLocalCapacity = kAbilityLocalSlotCapacity;
inline constexpr std::size_t kAbilityEntityListCapacity = 8;

inline constexpr std::size_t kAbilityPhaseCapacity = 16;
inline constexpr std::size_t kAbilityCostGroupCapacity = 8;
inline constexpr std::size_t kAbilityRecordLeaseCapacity = 8;
inline constexpr std::size_t kAbilityOwnedTagCapacity = 4;
inline constexpr std::size_t kAbilityConditionDepth = 3;
inline constexpr std::size_t kAbilityRequestCapacity = 32;

// Discrete cues an actor keeps until the oldest is overwritten: a peer misses one
// only if this many newer ones are made before a snapshot carrying it arrives.
inline constexpr std::size_t kAbilityCueLogCapacity = 8;

// A phase's presents, and a cue's sustained presentations, are numbered in eight
// bits each where a sustained presentation names the lease it holds.
inline constexpr std::size_t kAbilityMaxPhasePresents = 255;
inline constexpr std::size_t kAbilityMaxSustainedPresentations = 255;
