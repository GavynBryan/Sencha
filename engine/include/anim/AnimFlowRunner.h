#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimPredicate.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRigBinding.h>

//=============================================================================
// Flow advance
//
// Moves one layer through its flow for one tick. The flow's position is its
// section, the tick the section began and its loop count; everything else --
// how long the section runs, whether it loops, where it goes next -- is read
// from the bound flow, the facts and the request driving the layer.
//
// A section ends on the first tick at or past its clip's length, and the next
// begins on that tick, so every machine agrees on the tick a section changes.
// At a section's end the flow goes to the cancel section when cancelling,
// otherwise loops (while its predicate holds, or until its request's count),
// otherwise takes the first forward branch whose predicate holds, otherwise
// moves to the next section; past the last section, or at the end of the
// cancel section, it is complete and holds its final pose. An immediate cancel
// goes to the cancel section at once.
//
// This decides and records; it never writes a request. What a request should
// carry -- the authority's anchor, the tail that keeps a cancelled request for
// a late joiner while its flow plays out -- is returned for the caller to
// apply, because the caller owns write access to the request set.
//=============================================================================

struct AnimFlowTick
{
    const AnimBoundRig* Rig = nullptr;
    const AnimBoundFlow* Flow = nullptr;
    // Facts, tags and requests, for loop and branch predicates and for
    // resolving a slot section.
    const AnimPredicateInputs* Inputs = nullptr;
    // The request driving the layer, or null.
    const AnimRequest* Request = nullptr;
    AnimTick Now = 0;
    double TickSeconds = 0.0;
    // The layer is new to this flow this tick.
    bool Entered = false;
    // The flow is to go to its cancel section.
    bool Cancelling = false;
    // This machine plays the authority's flow rather than deciding it: an
    // anchor that says the flow is elsewhere moves it there.
    bool FollowsAnchor = false;
};

struct AnimFlowOutcome
{
    // The flow entered a section, or started, this tick: the anchor the
    // authority stamps on the driving request.
    bool SectionChanged = false;
    // It is playing out a cancelled request: keep that request's tail.
    bool KeepTail = false;
    bool Complete = false;
};

// Advances `flow` and sets the layer's playing clip, clip start and time.
AnimFlowOutcome AdvanceAnimFlow(const AnimFlowTick& tick, AnimLayerContent& layer, AnimLayerFlow& flow,
                                std::uint8_t layerIndex, AnimDecisionLog* log);
