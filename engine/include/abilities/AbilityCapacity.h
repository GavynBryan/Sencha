#pragma once

#include <cstddef>

// Starting points measured against the first content, not product limits. Every
// one is part of a component layout or a compiled table, so changing one is an ABI
// change; the compiler reports content that exceeds one as `capacity`.

inline constexpr std::size_t kAbilityGrantCapacity = 16;
inline constexpr std::size_t kAbilityLiveRecordCapacity = 4;
inline constexpr std::size_t kAbilityLingeringRecordCapacity = 2;
inline constexpr std::size_t kAbilityRecordCapacity = kAbilityLiveRecordCapacity + kAbilityLingeringRecordCapacity;

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
