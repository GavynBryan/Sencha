#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimPredicate.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRigBinding.h>

struct AnimFlowAdvanceInput
{
    const AnimBoundRig* Rig = nullptr;
    const AnimBoundFlow* Flow = nullptr;
    // For loop and branch predicates and slot section resolution.
    const AnimPredicateInputs* Inputs = nullptr;
    const AnimRequest* Request = nullptr;
    AnimTick Now = 0;
    double TickSeconds = 0.0;
    // The layer is new to this flow this tick.
    bool Entered = false;
    // The rig bound again since the last tick: indices the flow state holds may name
    // other sections and clips now.
    bool Rebound = false;
    bool Cancelling = false;
    // Plays the authority's flow rather than deciding it: an anchor elsewhere moves it.
    bool FollowsAnchor = false;
};

struct AnimFlowOutcome
{
    // Entered a section or started this tick, so the authority stamps an anchor.
    bool SectionChanged = false;
    // It is playing out a cancelled request: keep that request's tail.
    bool KeepTail = false;
    bool Complete = false;
};

// Sets the layer's playing clip, clip start and time. Never writes a request: the
// caller applies the anchor and tail the outcome asks for, as it owns the request set.
AnimFlowOutcome AdvanceAnimFlow(const AnimFlowAdvanceInput& tick, AnimLayerContent& layer, AnimLayerFlow& flow,
                                std::uint8_t layerIndex, AnimDecisionLog* log);
