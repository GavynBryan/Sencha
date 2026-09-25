#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class AnimationClipCache;
class SkeletonCache;

//=============================================================================
// Rig recipe
//
// What a new rig starts from: a name, the clips it plays and the tier it is.
// The plan is the documents a rig is -- a behavior set, a slot map, a request
// schema, selectors where the tier has them, and the rig -- plus the scenario
// saved beside it, so a new rig opens ready to play, with every name it uses
// declared as a preview fixture. The skeleton is the clips' own; clips of
// different skeletons do not make one rig.
//
// The tiers are the plan of record's, as starting points, not kinds:
//
//   Prop       one layer, no selector. The first clip idles; every other
//              clip plays while a request of its name is held.
//   Simple     one layer and a selector over the engine's facts. The first
//              clip idles, the second (if any) plays while Speed is above
//              0.1, and every other clip is an action a request plays once
//              through.
//   Character  Simple's layer for idle and locomotion, and an upper-body
//              layer masked from a chosen joint that plays the actions and is
//              shown only while one is requested.
//=============================================================================

enum class AnimationRigPreset : std::uint8_t
{
    Prop,
    Simple,
    Character,
};

struct AnimationRigRecipe
{
    // Letters, digits and underscores: the folder and file stem.
    std::string Name;
    // Clip asset paths; the first is the idle.
    std::vector<std::string> Clips;
    AnimationRigPreset Preset = AnimationRigPreset::Prop;
    // Character: the joint the upper body starts at, by name.
    std::string UpperBodyJoint;
};

[[nodiscard]] std::string_view AnimationRigPresetName(AnimationRigPreset preset);

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

// `skeletons`, when given, checks the upper-body joint against the clips'
// skeleton.
[[nodiscard]] AnimationRigPlan PlanAnimationRig(const AnimationRigRecipe& recipe, const AnimationClipCache& clips,
                                                const SkeletonCache* skeletons = nullptr);

// The behavior a clip plays as in a new rig: Anim. and the clip's name,
// letters, digits and underscores only.
[[nodiscard]] std::string AnimationRigBehaviorFor(const std::string& clipPath);
