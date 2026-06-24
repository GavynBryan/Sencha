#pragma once

#include <ecs/EntityId.h>
#include <framework/abilities/AbilityId.h>

#include <vector>

//=============================================================================
// Ability activation intents
//
// Input/AI produces intents; AbilityActivationSystem consumes them. Decoupling
// activation from raw input lets the same path serve player and AI.
//=============================================================================
struct AbilityActivation
{
    EntityId Actor;
    AbilityId Ability;
};

// World resource: pending activation intents drained by ProcessAbilityActivations.
struct AbilityActivationQueue
{
    std::vector<AbilityActivation> Pending;
};
