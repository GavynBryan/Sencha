#pragma once

#include <ecs/ComponentTypeId.h>

#include <type_traits>

//=============================================================================
// MovementProfile
//
// Per-entity movement feel (acceleration, friction, air control, jump). Data on
// the entity, not constants in the systems, so a designer changes it per character
// without touching code (the per-entity replacement for a hardcoded movement
// class). Top speed is not here: it is the MoveSpeed attribute, so sprint/slow/
// haste can modify it through effects. Units are meters and seconds.
//=============================================================================
struct MovementProfile
{
    float Acceleration = 40.0f;
    float Friction = 40.0f;
    float AirControl = 0.3f;
    float JumpSpeed = 5.5f;
    float CoyoteSeconds = 0.1f;
};

static_assert(std::is_trivially_copyable_v<MovementProfile>,
              "MovementProfile must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(MovementProfile, "sencha.movement_profile");
