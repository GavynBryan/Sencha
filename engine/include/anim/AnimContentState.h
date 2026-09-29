#pragma once

#include <anim/AnimPlayback.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

struct AnimLayerContent
{
    // The request driving this content instance, when one does. A request
    // that supersedes it and resolves other content starts a new instance.
    AnimRequestId Request;
    // The request's start as last seen here. A different start is the authority
    // correcting a guess, and restarts the instance.
    AnimTick RequestStartTick = 0;
    AnimTick StartTick = 0;
    GameplayTagId Behavior;
    std::uint32_t RowKey = 0;
    float TimeSeconds = 0.0f;
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // One-shot and flow content is fixed at behavior entry.
    bool Pinned = false;
    // Read by the next tick's selection: the one read against the dependency order.
    bool ContentComplete = false;
    // Playback carries on the phase of what played before it, rather than starting.
    bool Carried = false;
    // The clip actually playing (for a flow, the current section's) and where it is.
    // Events, sampling and root motion read these, not Content.
    std::uint16_t Clip = kAnimNoContent;
    AnimPlayback Playback;
    // Blendspace phase, shared by every sample, and this tick's coordinates. Clip is
    // the heaviest sample, whose events play.
    float Phase = 0.0f;
    float Coordinates[2] = {};
};

struct SENCHA_COMPONENT("sencha.anim_content_state") AnimContentState
{
    AnimLayerContent Layers[kAnimMaxLayers] = {};
    std::uint64_t BindingGeneration = 0;
    // This machine's rig binding does not match the timing on the authority's requests.
    bool TimingDisagrees = false;
    // A prediction the authority refused: the next pass restarts selection and
    // rebuilds request-driven layers from the authority's requests, as a joiner would.
    bool Reconstruct = false;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimContentState.sencha.h>
#endif
