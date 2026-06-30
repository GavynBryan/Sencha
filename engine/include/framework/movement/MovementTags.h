#pragma once

#include <framework/gameplay_tags/GameplayTagId.h>

class GameplayTagRegistry;

//=============================================================================
// MovementTags
//
// Resolved ids for the movement.* state hierarchy, stored as a World resource so
// the systems read ids without re-resolving names each tick. Ids are per-registry
// (registration order), so this lives beside the GameplayTagRegistry that minted
// it. The locomotion state is exposed as these hierarchical tags so other systems
// (animation, abilities) can query it: a query for movement.grounded matches any
// grounded substate via the registry's inclusive descendant test.
//=============================================================================
struct MovementTags
{
    GameplayTagId Controlled;
    GameplayTagId Grounded;
    GameplayTagId GroundedIdle;
    GameplayTagId GroundedWalking;
    GameplayTagId GroundedSprinting;
    GameplayTagId Airborne;
};

// Register the movement.* tag hierarchy and return the resolved ids. Idempotent:
// re-registration of the same names returns the existing ids.
MovementTags RegisterMovementTags(GameplayTagRegistry& registry);
