#pragma once

#include <ecs/ComponentTypeId.h>

#include <type_traits>

//=============================================================================
// MovementProfile
//
// Per-entity movement tunables (speed, acceleration, jump, air control). Feel is
// data on the entity, not constants in the systems, so a designer changes it per
// character without touching code (the data-driven knob set, the per-entity
// replacement for a hardcoded movement class). Units are meters and seconds.
//=============================================================================
struct MovementProfile
{
    float MaxSpeed = 6.0f;
    float Acceleration = 40.0f;
    float Friction = 40.0f;
    float AirControl = 0.3f;
    float SprintMultiplier = 1.6f;
    float JumpUpSpeed = 5.5f;
    float CoyoteSeconds = 0.1f;
};

static_assert(std::is_trivially_copyable_v<MovementProfile>,
              "MovementProfile must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementProfile, "sencha.movement_profile");
