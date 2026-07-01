#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>

#include <type_traits>

//=============================================================================
// MovementState
//
// Runtime kinematic state the locomotion operations own for a character.
// PlanarVelocity is the smoothed planar velocity carried between ticks
// (acceleration and friction integrate over it) and is written out to the
// character controller as the desired velocity. Contact state and the jump impulse
// are read/written directly on the physics CharacterController, not mirrored here.
//=============================================================================
struct MovementState
{
    Vec3d PlanarVelocity = Vec3d::Zero();
};

static_assert(std::is_trivially_copyable_v<MovementState>,
              "MovementState must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementState, "sencha.movement_state");
