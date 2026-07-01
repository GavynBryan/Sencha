#pragma once

#include <ecs/ComponentTypeId.h>

#include <type_traits>

//=============================================================================
// Locomotion mode markers
//
// Zero-size ECS marker components. Exactly one is present on a controlled
// character; the per-mode locomotion system dispatches on membership
// (GroundLocomotionSystem visits OnGround, AirLocomotionSystem visits InAir). Mode
// lives in archetype membership, not a tag-container value, so a new mode (slide,
// swim, climb) is a new marker plus its own system and one transition edge,
// editing no existing system. The mode arbiter is the single marker writer.
//=============================================================================
struct OnGround
{
};

static_assert(std::is_empty_v<OnGround>, "OnGround is a zero-size marker");

SENCHA_DECLARE_COMPONENT_TYPE(OnGround, "sencha.movement.on_ground");

struct InAir
{
};

static_assert(std::is_empty_v<InAir>, "InAir is a zero-size marker");

SENCHA_DECLARE_COMPONENT_TYPE(InAir, "sencha.movement.in_air");
