#include "authoring/AnimationBoneProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigDocumentEdits.h"
#include "authoring/AnimationRigEdits.h"

#include "properties/SettingProperties.h"

#include <anim/Skeleton.h>

#include <array>
#include <format>

namespace
{
struct MaskStep
{
    std::string_view Verb;
    std::string_view Label;
    bool Exclude;
    bool Subtree;
};
constexpr std::array<MaskStep, 4> kMaskSteps{ {
    { "add_below", "Add with Everything Below", false, true },
    { "add", "Add This Joint Only", false, false },
    { "remove_below", "Remove with Everything Below", true, true },
    { "remove", "Remove This Joint Only", true, false },
} };

const SkeletonJoint* PickedJoint(AnimationPreviewWorkspace& workspace)
{
    const SkeletonData& skeleton = workspace.Audition.Session.Skeleton();
    const int joint = workspace.Rig.Navigation.Joint;
    return joint >= 0 && static_cast<std::size_t>(joint) < skeleton.Joints.size()
               ? &skeleton.Joints[static_cast<std::size_t>(joint)]
               : nullptr;
}
} // namespace

std::vector<PropertyNode> AnimationBoneProperties::Nodes() const
{
    const SkeletonJoint* joint = PickedJoint(Workspace);
    if (joint == nullptr)
        return {};
    const SkeletonData& skeleton = Workspace.Audition.Session.Skeleton();
    const std::size_t index = static_cast<std::size_t>(Workspace.Rig.Navigation.Joint);
    std::vector<PropertyNode> nodes{ NoteProperty(
        "parent", "Parent",
        joint->ParentIndex >= 0 ? skeleton.Joints[static_cast<std::size_t>(joint->ParentIndex)].Name
                                : std::string("None, the root")) };

    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr)
    {
        nodes.push_back(NoteProperty("mask", "Masks", "Open a rig to mask its layers."));
        return nodes;
    }
    const std::vector<std::uint8_t> coverage = AnimMaskCoverage(*rig);
    PropertyNode layers = GroupProperty("layers", "Layers covering it");
    for (std::size_t layer = 0; layer < rig->Layers.size(); ++layer)
    {
        const bool covered = index < coverage.size() && (coverage[index] & (1u << layer)) != 0;
        layers.Children.push_back(NoteProperty(std::format("layers.{}", layer), rig->Layers[layer].NameText,
                                               covered ? "Covers it" : "Not over it"));
    }
    nodes.push_back(std::move(layers));

    const std::size_t layer = Workspace.Rig.Navigation.Layer;
    if (layer == 0 || layer >= rig->Layers.size())
    {
        nodes.push_back(NoteProperty("mask", "Mask", "Select a layer above the first to mask it."));
        return nodes;
    }
    PropertyNode mask = NoteProperty("mask", "Mask", std::format("Of {}", rig->Layers[layer].NameText));
    for (const MaskStep& step : kMaskSteps)
        mask.Descriptor.Verbs.push_back({ std::string(step.Verb), std::string(step.Label) });
    nodes.push_back(std::move(mask));
    return nodes;
}

PropertyEditResult AnimationBoneProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const SkeletonJoint* joint = PickedJoint(Workspace);
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    const std::size_t layer = Workspace.Rig.Navigation.Layer;
    if (key != "mask" || joint == nullptr || joint->Name.empty() || rig == nullptr || layer == 0
        || layer >= rig->Layers.size())
        return PropertyEditResult::Gone;
    for (const MaskStep& step : kMaskSteps)
        if (step.Verb == verb)
            return EditAnimationRig(Workspace.Documents, Workspace.Rig.Path,
                                    [&](JsonValue& root) {
                                        return AddAnimMaskStep(root, layer, joint->Name, step.Exclude, step.Subtree);
                                    },
                                    Workspace.DocumentError)
                       ? PropertyEditResult::Applied
                       : PropertyEditResult::Refused;
    return PropertyEditResult::Gone;
}
