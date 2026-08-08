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
    // Projected from SupportState by SupportTagProjectionSystem, for the systems
    // that query locomotion state (animation, abilities, mode entry). There is
    // no airborne counterpart: it is the absence of this tag, and a query says
    // so with None rather than a second tag tracking the same fact.
    //
    // Jump does not read it. Jump reads SupportState directly, because a
    // predicted step has to see the support the sweep just produced rather than
    // a tag projected from the step before it.
    GameplayTagId Grounded;
};

// Register the movement.* tag hierarchy and return the resolved ids. Idempotent:
// re-registration of the same names returns the existing ids.
MovementTags RegisterMovementTags(GameplayTagRegistry& registry);
