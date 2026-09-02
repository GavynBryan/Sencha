#pragma once

#include <ecs/ComponentAnnotations.h>

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
//
// Owner-only: the machine resuming this character's simulation has to know
// whether it may jump yet, and nobody else has any use for the number. A
// replayed tick evaluates the same gate the live tick did, so the owner resumes
// from the authority's cooldown rather than its own guess at it.
//=============================================================================
struct SENCHA_COMPONENT("sencha.jump_state")
       SENCHA_SCHEMA("JumpState")
       SENCHA_REPLICATED
       SENCHA_PREDICTED
JumpState
{
    // Seconds until this character may jump again. Counts down every step,
    // including steps where no jump was asked for.
    SENCHA_FIELD("cooldown_remaining")
    SENCHA_OWNER_ONLY
    float CooldownRemaining = 0.0f;
};

static_assert(std::is_trivially_copyable_v<JumpState>,
              "JumpState must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <movement/JumpState.sencha.h>
#endif
