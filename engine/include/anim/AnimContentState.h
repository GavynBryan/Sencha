#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

//=============================================================================
// AnimContentState
//
// What each layer is playing: the behavior, the slot row that resolved it, the
// content, and where in it. Content is an index into the bound rig's content
// table, never an asset handle -- the rig's slot maps hold the clips -- and a
// row is named by index and stable key so a reload can tell whether pinned
// content still exists.
//
// ContentComplete is the one value the next tick's selection reads back: it is
// published here after resolution and read one tick late, which is the only
// read against the dependency order and never a fact.
//=============================================================================

struct AnimLayerContent
{
    // The request driving this content instance, when one does. A request
    // that supersedes it and resolves other content starts a new instance.
    AnimRequestId Request;
    AnimTick StartTick = 0;
    GameplayTagId Behavior;
    std::uint32_t RowKey = 0;
    // Seconds into the content at StartTick: a row change carries normalized
    // time across by starting the new content here.
    float StartOffsetSeconds = 0.0f;
    float TimeSeconds = 0.0f;
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // One-shot and flow content is fixed at behavior entry.
    bool Pinned = false;
    bool ContentComplete = false;
    // What is actually playing: the content's clip, or for a flow the current
    // section's clip, the tick that clip began and how far into it it began.
    // TimeSeconds is time in this clip. Events, sampling and the editor read
    // these, never Content, for what to play.
    std::uint16_t Clip = kAnimNoContent;
    AnimTick ClipStartTick = 0;
    float ClipOffsetSeconds = 0.0f;
    // How far the event pass has taken this content: the content instance it
    // was reading (named by its StartTick) and the last tick it covered. A
    // new instance, or a gap in the ticks covered, is how the pass tells an
    // entry from a continuation and a continuation from a skip.
    AnimTick EventStartTick = kAnimNoTick;
    AnimTick EventTick = kAnimNoTick;
    // The behavior the event pass last saw on this layer: a change is a
    // lifecycle exit and entry.
    GameplayTagId EventBehavior;
    // The flow section it last saw, and the content instance (by content and
    // start) it belonged to: a change is a section exit and entry.
    AnimTick EventContentStartTick = kAnimNoTick;
    std::uint16_t EventContent = kAnimNoContent;
    std::uint8_t EventSection = 0xFF;
};

struct SENCHA_COMPONENT("sencha.anim_content_state") AnimContentState
{
    AnimLayerContent Layers[kAnimMaxLayers] = {};
    std::uint64_t BindingGeneration = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimContentState.sencha.h>
#endif
