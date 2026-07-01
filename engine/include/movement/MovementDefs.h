#pragma once

#include <abilities/AbilityId.h>
#include <attributes/AttributeId.h>

//=============================================================================
// MovementDefs
//
// Resolved non-tag ids the locomotion systems read without re-resolving names
// each tick, stored as a World resource beside MovementTags and minted by the
// movement registration functions:
//   MoveSpeed  the target-speed attribute (buffable, so sprint/slow/haste modify
//              it through effects rather than a branch in the locomotion code).
//   Jump       the ability input activates; its gating (grounded, cooldown, cost)
//              is AbilityKit data, not a hardcoded jump check.
//=============================================================================
struct MovementDefs
{
    AttributeId MoveSpeed;
    AbilityId Jump;
};
