#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <framework/effects/EffectId.h>

#include <type_traits>

//=============================================================================
// ActiveEffect
//
// One applied Duration/Infinite effect, living on its OWN entity and referencing
// the target by EntityId (the effects-as-entities model). Spawned by ApplyEffect,
// ticked and expired by TickEffects, folded into target attributes by
// FoldActiveEffects. Stacking is just multiple ActiveEffect entities on the same
// target; their modifiers fold additively and their granted tags ref-count.
//
// Runtime-only state: not scene-serialized (persistence is the streaming
// overlay's job). Trivially copyable so it lives in chunks.
//=============================================================================
struct ActiveEffect
{
    EntityId Target;
    EffectId Def;
    float TimeRemaining = 0.0f; // seconds; negative = infinite
    float PeriodTimer = 0.0f;   // counts down to the next periodic application
};

static_assert(std::is_trivially_copyable_v<ActiveEffect>,
              "ActiveEffect must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(ActiveEffect, "sencha.active_effect");
