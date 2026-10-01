#include "authoring/AnimationRigCreation.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>

#include <algorithm>
#include <array>
#include <format>

namespace
{
constexpr std::array<std::string_view, 3> kTiers{ "Prop", "Simple", "Character" };
constexpr std::array<std::string_view, 3> kTierHelp{
    "One layer, no selector. The first clip idles; every other clip plays while a request of its name is held. "
    "Doors, machinery, pickups.",
    "One layer chosen by rules over the engine's facts. The first clip idles, the second plays while moving, and "
    "the rest are actions a request plays once through. Most enemies.",
    "Simple's idle and locomotion, and an upper-body layer from a chosen joint that plays the actions over them. "
    "Players and anything that acts while it moves.",
};
constexpr std::string_view kClipKey = "clips.";
} // namespace

std::string AnimationRigCreation::SkeletonOf(const std::string& clip) const
{
    const AnimationClipCache& clips = Workspace.Clips();
    const AnimationClipData* data = clips.Get(clips.Find(clip));
    return data != nullptr ? data->SkeletonPath : std::string();
}

std::string AnimationRigCreation::ChosenSkeleton() const
{
    return Recipe.Clips.empty() ? std::string() : SkeletonOf(Recipe.Clips.front());
}

std::vector<PropertyNode> AnimationRigCreation::Nodes() const
{
    const std::size_t tier = static_cast<std::size_t>(Recipe.Preset);
    std::vector<PropertyNode> nodes{
        TextProperty("name", "Name", Recipe.Name, "The rig's name; its documents are named after it"),
        ChoiceProperty("tier", "Tier", kTiers, tier, std::string(kTierHelp[tier])),
    };

    const std::string skeleton = ChosenSkeleton();
    PropertyNode clips = GroupProperty("clips", "Clips");
    clips.Descriptor.Tooltip = "The first chosen clip idles. One rig poses one skeleton.";
    for (const std::string& path : Workspace.Content.Of(AssetType::AnimationClip))
    {
        const auto chosen = std::ranges::find(Recipe.Clips, path);
        PropertyNode clip = ToggleProperty(std::string(kClipKey) + path,
                                           chosen == Recipe.Clips.begin() ? path + "  (idle)" : path,
                                           chosen != Recipe.Clips.end());
        clip.Descriptor.ReadOnly = chosen == Recipe.Clips.end() && !skeleton.empty() && SkeletonOf(path) != skeleton;
        clips.Children.push_back(std::move(clip));
    }
    nodes.push_back(std::move(clips));

    if (Recipe.Preset == AnimationRigPreset::Character)
    {
        const SkeletonData* data =
            skeleton.empty() ? nullptr : Workspace.Skeletons().Get(Workspace.Skeletons().Find(skeleton));
        if (data == nullptr)
            nodes.push_back(NoteProperty("upper", "Upper body from", "Choose clips first; the joints are theirs."));
        else
        {
            std::vector<std::string_view> joints;
            std::size_t current = 0;
            for (const SkeletonJoint& joint : data->Joints)
            {
                if (joint.Name == Recipe.UpperBodyJoint)
                    current = joints.size();
                joints.push_back(joint.Name);
            }
            nodes.push_back(ChoiceProperty("upper", "Upper body from", joints, current));
        }
    }

    PropertyNode create = NoteProperty("create", "Writes", "A behavior set, slot map, request schema, selectors, "
                                                           "rig and scenario, then opens the rig.");
    if (!Recipe.Name.empty() && !Recipe.Clips.empty())
        create.Descriptor.Verbs = { { "create", "Create Rig" } };
    nodes.push_back(std::move(create));
    if (!Error.empty())
        nodes.push_back(NoteProperty("error", "Refused", Error));
    return nodes;
}

PropertyEditResult AnimationRigCreation::CommitEdit(std::string_view key, std::string_view value)
{
    if (key == "name")
    {
        if (Recipe.Name == value)
            return PropertyEditResult::Unchanged;
        Recipe.Name = value;
        return PropertyEditResult::Applied;
    }
    if (key == "tier")
    {
        const std::optional<std::size_t> tier = ParseChoice(value, kTiers);
        if (!tier)
            return PropertyEditResult::Refused;
        Recipe.Preset = static_cast<AnimationRigPreset>(*tier);
        return PropertyEditResult::Applied;
    }
    if (key == "upper")
    {
        Recipe.UpperBodyJoint = value;
        return PropertyEditResult::Applied;
    }
    if (key.starts_with(kClipKey))
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on)
            return PropertyEditResult::Refused;
        const std::string path(key.substr(kClipKey.size()));
        const auto chosen = std::ranges::find(Recipe.Clips, path);
        if (*on == (chosen != Recipe.Clips.end()))
            return PropertyEditResult::Unchanged;
        if (*on)
        {
            if (const std::string skeleton = ChosenSkeleton(); !skeleton.empty() && SkeletonOf(path) != skeleton)
                return PropertyEditResult::Refused;
            Recipe.Clips.push_back(path);
        }
        else
            Recipe.Clips.erase(chosen);
        return PropertyEditResult::Applied;
    }
    return PropertyEditResult::Gone;
}

PropertyEditResult AnimationRigCreation::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key != "create" || verb != "create")
        return PropertyEditResult::Gone;
    if (!Workspace.CreateRig(Recipe, Error))
        return PropertyEditResult::Refused;
    Recipe = {};
    Error.clear();
    if (Created)
        Created();
    return PropertyEditResult::Applied;
}
