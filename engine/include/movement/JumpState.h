#pragma once

#include <ecs/ComponentTypeId.h>

#include <type_traits>

//=============================================================================
// JumpState
//
// What a character's jump remembers between ticks.
//
// Plain state on the character rather than a timed effect, because a client
// predicting its own movement replays the ticks the authority has not answered
// yet, and a replayed tick has to evaluate the same gate the live tick did.
// State can be restored and re-read; an effect that exists as its own entity
// cannot be re-created inside the phase reconciliation runs in.
//=============================================================================
struct JumpState
{
    // Seconds until this character may jump again. Counts down every step,
    // including steps where no jump was asked for.
    float CooldownRemaining = 0.0f;
};

static_assert(std::is_trivially_copyable_v<JumpState>,
              "JumpState must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(JumpState, "sencha.jump_state");
