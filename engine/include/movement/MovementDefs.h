#pragma once

#include <attributes/AttributeId.h>

//=============================================================================
// MovementDefs
//
// Resolved non-tag ids the locomotion systems read without re-resolving names
// each tick, stored as a World resource beside MovementTags and minted by the
// movement registration functions:
//   MoveSpeed  the target-speed attribute (buffable, so sprint/slow/haste modify
//              it through effects rather than a branch in the locomotion code).
//=============================================================================
struct MovementDefs
{
    AttributeId MoveSpeed;
};
