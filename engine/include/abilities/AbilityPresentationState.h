#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityId.h>
#include <ecs/ComponentAnnotations.h>

#include <cstdint>
#include <type_traits>

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
        // The record's start order on the actor; zero for an empty slot.
        std::uint64_t Activation = 0;
        AbilityId Ability;
        std::uint32_t Pulses = 0;
        std::uint8_t SustainedPhase = kAbilityNotSustained;
    };

    Shown Records[kAbilityRecordCapacity] = {};
    std::uint16_t CueSeen = 0;
    AbilityPresentationFollow Follow = AbilityPresentationFollow::Unseen;
};

static_assert(std::is_trivially_copyable_v<AbilityPresentationState>);

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityPresentationState.sencha.h>
#endif
