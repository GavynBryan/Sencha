#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class AnimationClipCache;
class SkeletonCache;

// The presets are described in this editor's README.
enum class AnimationRigPreset : std::uint8_t
{
    Prop,
    Simple,
    Character,
};

struct AnimationRigRecipe
{
    // Letters, digits and underscores; used as the folder and file stem.
    std::string Name;
    // The first clip is the idle.
    std::vector<std::string> Clips;
    AnimationRigPreset Preset = AnimationRigPreset::Prop;
    // Character preset only.
    std::string UpperBodyJoint;
};

[[nodiscard]] std::string_view AnimationRigPresetName(AnimationRigPreset preset);

struct AnimationNewDocument
{
    // Relative to the authoring content root.
    std::string RelativePath;
    JsonValue Root;
};

struct AnimationRigPlan
{
    std::string Error;
    std::vector<AnimationNewDocument> Documents;
    AnimationNewDocument Scenario;
    std::string RigPath;
};

// Without `skeletons` the upper-body joint is not checked.
[[nodiscard]] AnimationRigPlan PlanAnimationRig(const AnimationRigRecipe& recipe, const AnimationClipCache& clips,
                                                const SkeletonCache* skeletons = nullptr);

// "Anim." plus the clip's name reduced to letters, digits and underscores.
[[nodiscard]] std::string AnimationRigBehaviorFor(const std::string& clipPath);
