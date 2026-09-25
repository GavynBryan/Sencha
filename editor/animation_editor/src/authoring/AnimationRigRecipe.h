#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class AnimationClipCache;
class SkeletonCache;

// A new rig from a name, its clips and a tier: the documents it is made of and
// the scenario saved beside it. The tiers are in this editor's README.

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
