#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>

#include <type_traits>

//=============================================================================
// MovementState
//
// Runtime kinematic state the movement systems own for a character. PlanarVelocity
// is the smoothed planar velocity carried between ticks (acceleration and friction
// integrate over it) and is the value a backend reads as the desired velocity.
// CoyoteTimer is the jump grace remaining just after leaving the ground. Grounded
// and JumpRequest are the seam to a locomotion backend (the physics character
// controller in this engine, but the framework stays backend-free): a bridge feeds
// Grounded in from contacts before the tick, and reads JumpRequest out after it.
//=============================================================================
struct MovementState
{
    Vec3d PlanarVelocity = Vec3d::Zero();
    float CoyoteTimer = 0.0f;
    float JumpRequest = 0.0f;
    bool Grounded = false;
};

static_assert(std::is_trivially_copyable_v<MovementState>,
              "MovementState must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementState, "sencha.movement_state");
