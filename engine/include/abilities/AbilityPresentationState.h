#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityId.h>
#include <ecs/ComponentAnnotations.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

inline constexpr std::size_t kAbilityGuessedCueMemory = 16;

// Phase a record's sustained presentation is for, when none is held.
inline constexpr std::uint8_t kAbilityNotSustained = 0xFF;

enum class AbilityPresentationFollow : std::uint8_t
{
    // Not yet looked at: what the actor already shows is taken as presented.
    Unseen,
    Following,
};

// What this machine has presented of an actor's abilities. Machine-local: never
// replicated, never journaled; every presenting machine keeps its own.
struct SENCHA_COMPONENT("sencha.ability_presentation_state") AbilityPresentationState
{
    struct Shown
    {
        // The record's identity (AbilityActivationIdentity); zero for an empty slot.
        std::uint64_t Activation = 0;
        AbilityId Ability;
        std::uint32_t Pulses = 0;
        std::uint8_t SustainedPhase = kAbilityNotSustained;
    };

    Shown Records[kAbilityRecordCapacity] = {};
    // Records that went away, so one the wire brings back is not shown again.
    Shown Departed[kAbilityRecordCapacity] = {};
    std::uint8_t NextDeparted = 0;
    std::uint16_t CueSeen = 0;
    // The owner's guessed cues: the last serial presented, and the occurrences
    // (AbilityCueOccurrence) whose authority entries are not presented again.
    std::uint16_t GuessedCueSeen = 0;
    std::uint8_t NextGuessed = 0;
    std::uint64_t Guessed[kAbilityGuessedCueMemory] = {};
    AbilityPresentationFollow Follow = AbilityPresentationFollow::Unseen;
};

static_assert(std::is_trivially_copyable_v<AbilityPresentationState>);

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityPresentationState.sencha.h>
#endif
