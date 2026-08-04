#pragma once

#include <gameplay_tags/GameplayTagId.h>

class GameplayTagRegistry;

//=============================================================================
// MovementTags
//
// Resolved ids for the movement.* state hierarchy, stored as a World resource so
// the systems read ids without re-resolving names each tick. Ids are registration
// order, so this lives beside the GameplayTagRegistry that minted it. Locomotion
// state is exposed as these hierarchical tags so other systems (animation,
// abilities) can query it: a query for movement.grounded matches any grounded
// substate via the registry's inclusive descendant test.
//
// Per-mode active and request tags are not here: the LocomotionModeRegistry
// mints those when a mode registers, so a game's modes get them too.
//=============================================================================
struct MovementTags
{
    GameplayTagId Controlled;
    // Projected from SupportState by SupportTagProjectionSystem; the Jump gate.
    // There is no airborne counterpart: it is the absence of this tag, and a
    // query says so with None rather than a second tag tracking the same fact.
    GameplayTagId Grounded;
    GameplayTagId JumpRequested; // one-tick: granted on jump activation, consumed by jump execution
    GameplayTagId JumpCooldown;  // blocks the Jump ability until its cooldown effect expires
};

// Register the movement.* tag hierarchy and return the resolved ids. Idempotent:
// re-registration of the same names returns the existing ids.
MovementTags RegisterMovementTags(GameplayTagRegistry& registry);
