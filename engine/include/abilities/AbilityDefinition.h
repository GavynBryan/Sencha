#pragma once

#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagQuery.h>

//=============================================================================
// AbilityDefinition, authored data.
//
// An ability is thin orchestration over effects + tags. Everything it does to
// the world is an effect:
//   - Cost       : instant effect (e.g. -Stamina), gated by affordability.
//   - Cooldown   : duration effect that grants a cooldown tag; the activation
//                  query blocks on that tag, so it cannot re-activate until the
//                  effect expires.
//   - OnActivate : the behavior — a duration effect granting state tags / buffs.
//
// Mutual exclusion between abilities is expressed as block clauses against each
// other's granted state tags (no central arbiter, no state machine).
//=============================================================================
struct AbilityDefinition
{
    GameplayTagQuery ActivationRequirements; // All=require, Any=require-any, None=block
    EffectId Cost;       // invalid = free
    EffectId Cooldown;   // invalid = no cooldown
    EffectId OnActivate; // invalid = no behavior effect
};
