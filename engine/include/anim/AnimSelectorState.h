#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

//=============================================================================
// AnimSelectorState
//
// Selection bookkeeping per layer, and nothing else: which rule is winning and
// since when, the previous winner, when a hold and each cooldown expire, and
// the latch record. It holds outcomes of selection only -- never a gameplay
// value, a content id, a clip time or pose data -- so selection stays a pure
// function of facts, requests, the previous tick's feedback, and this.
//
// Rules are named by their flattened index and by a stable key, so a rebind
// that reorders rules remaps the winner instead of resetting it.
//=============================================================================

enum class AnimLatchState : std::uint8_t
{
    None,
    Held,
    // The latching request ended and the latch plays its content out.
    Finishing,
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
    AnimLatchState Latch = AnimLatchState::None;
};

struct SENCHA_COMPONENT("sencha.anim_selector_state") AnimSelectorState
{
    AnimLayerSelection Layers[kAnimMaxLayers] = {};
    // The rig binding the indices above were taken against.
    std::uint64_t BindingGeneration = 0;
    // Digests of what the selectors read, so an entity whose inputs did not
    // change and whose timers have not come due is not re-evaluated, and a
    // winner change can say whether requests caused it.
    std::uint64_t FactDigest = 0;
    std::uint64_t RequestDigest = 0;
    bool Evaluated = false;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimSelectorState.sencha.h>
#endif
