#pragma once

#include <anim/AnimPlayback.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

// What the event pass last crossed on one layer, and what it last saw playing there;
// see docs/gameplay/animation.md, "Playback time".
struct AnimLayerEventCursor
{
    AnimPlayback Playback;
    // The tick this instance was crossed through; kAnimNoTick before any pass.
    AnimTick Tick = kAnimNoTick;
    // A change is a lifecycle exit and entry, the exit attributed to Request.
    GameplayTagId Behavior;
    AnimRequestId Request;
    AnimTick ContentStartTick = kAnimNoTick;
    std::uint16_t Clip = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    std::uint8_t Section = 0xFF;
    float Phase = 0.0f;
};

// The event pass's own state, beside the content it reads: nothing else writes it.
struct SENCHA_COMPONENT("sencha.anim_event_cursor") AnimEventCursor
{
    AnimLayerEventCursor Layers[kAnimMaxLayers] = {};
    // The binding the indices above were taken against.
    std::uint64_t BindingGeneration = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimEventCursor.sencha.h>
#endif
