#pragma once

#include <core/json/JsonValue.h>

#include <string>
#include <vector>

class AnimationClipCache;

//=============================================================================
// Rig recipe
//
// What a new rig starts from: a name and the clips it plays. The plan is the
// four documents a rig is -- a behavior set, a slot map, a request schema and
// the rig -- plus the scenario saved beside it, so a new rig opens ready to
// play: the first clip is its idle, every other clip is a request of the same
// name that plays while held, and the scenario declares every name the rig
// uses as a preview fixture. The skeleton is the clips' own; clips of
// different skeletons do not make one rig.
//=============================================================================

struct AnimationRigRecipe
{
    // Letters, digits and underscores: the folder and file stem.
    std::string Name;
    // Clip asset paths; the first is the idle.
    std::vector<std::string> Clips;
};

struct AnimationNewDocument
{
    // Relative to the content root the rig is written into.
    std::string RelativePath;
    JsonValue Root;
};

struct AnimationRigPlan
{
    // Empty when the plan stands; otherwise why it does not.
    std::string Error;
    std::vector<AnimationNewDocument> Documents;
    AnimationNewDocument Scenario;
    // The rig's asset path.
    std::string RigPath;
};

[[nodiscard]] AnimationRigPlan PlanAnimationRig(const AnimationRigRecipe& recipe, const AnimationClipCache& clips);

// The behavior a clip plays as in a new rig: Anim. and the clip's name,
// letters, digits and underscores only.
[[nodiscard]] std::string AnimationRigBehaviorFor(const std::string& clipPath);
