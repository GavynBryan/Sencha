#pragma once

#include <ecs/ComponentTypeId.h>

#include <type_traits>

//=============================================================================
// Locomotion mode markers
//
// Zero-size ECS marker components. Exactly one is present on a controlled
// character; the per-mode locomotion operation dispatches on membership
// (TickGroundLocomotion visits OnGround, TickAirLocomotion visits InAir). Mode
// lives in archetype membership, not a tag-container value, so a new mode (slide,
// swim, climb) is a new marker plus its own operation and one transition edge,
// editing no existing operation. The transition system is the single writer, and
// swaps markers through a CommandBuffer (structural change, never mid-iteration).
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
