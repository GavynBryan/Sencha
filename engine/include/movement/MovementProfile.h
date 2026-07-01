#pragma once

#include <ecs/ComponentTypeId.h>

#include <type_traits>

//=============================================================================
// MovementProfile
//
// Per-entity movement feel, the Quake locomotion knobs. Data on the entity, not
// constants in the systems, so a designer changes it per character without
// touching code. Top speed is not here: it is the MoveSpeed attribute, so
// sprint/slow/haste can modify it through effects. Acceleration is projected onto
// the wish direction (PM_Accelerate): it only tops up the component along wishDir
// up to wishSpeed, which preserves momentum and, with a low air cap, gives
// skill-based air control. Friction is a multiplicative decay with a floor
// (StopSpeed) so low speeds stop crisply while high speeds bleed smoothly. Units
// are meters and seconds; accel/friction are per-second coefficients.
//=============================================================================
struct MovementProfile
{
    float GroundAcceleration = 10.0f; // PM_Accelerate coefficient on the ground
    float AirAcceleration = 10.0f;    // PM_AirAccelerate coefficient
    float Friction = 6.0f;            // ground friction coefficient
    float StopSpeed = 1.0f;           // friction control floor: crisp low-speed stop
    float MaxAirSpeed = 1.0f;         // wish-speed cap for the air accel term (the air-strafe knob)
    float JumpSpeed = 5.5f;
};

static_assert(std::is_trivially_copyable_v<MovementProfile>,
              "MovementProfile must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementProfile, "sencha.movement_profile");
