#pragma once

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
    // Seconds into the content at StartTick, carrying normalized time across a row change.
    float StartOffsetSeconds = 0.0f;
    float TimeSeconds = 0.0f;
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // One-shot and flow content is fixed at behavior entry.
    bool Pinned = false;
    // Read by the next tick's selection: the one read against the dependency order.
    bool ContentComplete = false;
    // The clip actually playing (for a flow, the current section's), when it began
    // and at what offset. Events, sampling and the editor read these, not Content.
    std::uint16_t Clip = kAnimNoContent;
    AnimTick ClipStartTick = 0;
    float ClipOffsetSeconds = 0.0f;
    // Seconds of clip per second of ticks; negative plays backwards and zero holds.
    float ClipRate = 1.0f;
    // Blendspace phase, shared by every sample, and this tick's coordinates. Clip is
    // the heaviest sample, whose events play.
    float Phase = 0.0f;
    float Coordinates[2] = {};
    // The content instance (by StartTick) and last tick the event pass covered, which
    // tell an entry from a continuation and a continuation from a skip.
    AnimTick EventStartTick = kAnimNoTick;
    AnimTick EventTick = kAnimNoTick;
    // What the event pass last saw; a change is a lifecycle exit and entry.
    GameplayTagId EventBehavior;
    AnimTick EventContentStartTick = kAnimNoTick;
    std::uint16_t EventContent = kAnimNoContent;
    std::uint8_t EventSection = 0xFF;
    float EventPhase = 0.0f;
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
    // Each request-set slot's retained request as last seen, and whether any
    // layer has played it; a request that ends unplayed is counted.
    std::uint32_t RequestSeen[kAnimRequestCapacity] = {};
    std::uint8_t RequestPlayed = 0;
    std::uint32_t UnplayedRequests = 0;
};

static_assert(kAnimRequestCapacity <= 8, "RequestPlayed holds one bit per request slot.");

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimContentState.sencha.h>
#endif
