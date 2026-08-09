#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>

#include <type_traits>

//=============================================================================
// MovementIntent
//
// One tick of move request for a character, written by a producer (player input
// mapping, an AI controller) and read by the locomotion operations. Separating
// the request from its resolution is what lets input, AI, and the mover vary
// independently. WishDir is world-space and planar (the producer has already
// resolved any camera-relative framing); its magnitude in [0, 1] carries input
// strength.
//
// This is the whole of what a character's own motion depends on for a tick, and
// that is deliberate: a client predicting its own movement replays these ticks
// from the records it kept, so anything that steers a body has to arrive here
// rather than through a queue or an entity that replay cannot re-create.
// Actions that ask the authority for something -- fire, cast, interact -- go the
// other way, through AbilityKit, where being answered late is correct.
//=============================================================================
struct MovementIntent
{
    Vec3d WishDir = Vec3d::Zero();
    // Held, not edge-triggered: a replayed tick has to see what the live tick
    // saw, and an edge is a fact about two ticks rather than one.
    bool Jump = false;
};

static_assert(std::is_trivially_copyable_v<MovementIntent>,
              "MovementIntent must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementIntent, "sencha.movement_intent");
