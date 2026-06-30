#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>

#include <type_traits>

//=============================================================================
// MovementIntent
//
// One tick of planar move request for a character, written by a producer (player
// input mapping, an AI controller) and read by the movement systems. Separating
// the request from its resolution is what lets input, AI, and the mover vary
// independently. WishDir is world-space and planar (the producer has already
// resolved any camera-relative framing); its magnitude in [0, 1] carries input
// strength. JumpQueued is a one-shot consumed by MovementSystem each tick.
//=============================================================================
struct MovementIntent
{
    Vec3d WishDir = Vec3d::Zero();
    bool Sprint = false;
    bool JumpQueued = false;
};

static_assert(std::is_trivially_copyable_v<MovementIntent>,
              "MovementIntent must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementIntent, "sencha.movement_intent");
