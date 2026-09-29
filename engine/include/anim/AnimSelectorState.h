#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

enum class AnimLatchState : std::uint8_t
{
    None,
    Held,
    // The latching request ended and the latch plays its content out.
    Finishing,
    // Cancelled into a flow's cancel section; holds until that section completes.
    Cancelling,
};

struct AnimLayerSelection
{
    AnimTick WinnerStartTick = 0;
    AnimTick HoldUntilTick = 0;
    AnimTick LatchStartTick = 0;
    // The earliest tick a timer could change this layer's outcome.
    AnimTick WakeTick = kAnimNoTick;
    AnimTick CooldownUntil[kAnimCooldownSlots] = {};
    AnimRequestId LatchRequest;
    std::uint32_t WinnerKey = 0;
    GameplayTagId Behavior;
    std::uint16_t Winner = kAnimNoRule;
    std::uint16_t Previous = kAnimNoRule;
    // Index into the selector's weight rules; kAnimNoRule when the rig's constant applies.
    std::uint16_t WeightRule = kAnimNoRule;
    AnimLatchState Latch = AnimLatchState::None;
    // The layer's weight this tick, in [0, 1].
    float Weight = 1.0f;
};

// Selection outcomes only, never a gameplay value, content id, clip time or pose,
// so selection stays a pure function of facts, requests, feedback and this.
struct SENCHA_COMPONENT("sencha.anim_selector_state") AnimSelectorState
{
    AnimLayerSelection Layers[kAnimMaxLayers] = {};
    // The rig binding the indices above were taken against.
    std::uint64_t BindingGeneration = 0;
    // Digests of what selectors read: skip re-evaluation when unchanged, and tell
    // whether requests caused a winner change.
    std::uint64_t FactDigest = 0;
    std::uint64_t RequestDigest = 0;
    bool Evaluated = false;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimSelectorState.sencha.h>
#endif
