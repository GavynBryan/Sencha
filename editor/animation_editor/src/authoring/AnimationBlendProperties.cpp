#include "authoring/AnimationBlendProperties.h"

#include "authoring/AnimationBlendspacePad.h"
#include "authoring/AnimationLayerChoice.h"
#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimPoseEvaluation.h>
#include <anim/Skeleton.h>

#include <algorithm>
#include <format>

namespace
{
constexpr std::size_t kRecentBlends = 12;

std::string BlendingNow(const AnimationPreviewSession& session, const AnimPosePool::Slot& slot,
                        const AnimLayerPose& layer, std::size_t l)
{
    const double dt = session.TickSeconds();
    std::string text = session.TagName(layer.Playing.Behavior);
    if (layer.Fading)
    {
        const float elapsed = static_cast<float>(static_cast<double>(slot.Tick - layer.FadeStartTick) * dt);
        text += std::format(", crossfading from {} {:.0f} / {:.0f} ms", session.TagName(layer.FadingOut.Behavior),
                            elapsed * 1000.0f, std::max(layer.FadeInSeconds, layer.FadeOutSeconds) * 1000.0f);
    }
    if (layer.Offsetting)
    {
        const float elapsed = static_cast<float>(static_cast<double>(slot.Tick - layer.OffsetStartTick) * dt);
        const float remaining = AnimOffsetRemaining(
            std::span<const AnimJointOffset>(slot.Offsets).subspan(l * slot.Joints, slot.Joints), elapsed);
        text += std::format(", inertializing {:.0f} / {:.0f} ms, {:.3f} left", elapsed * 1000.0f,
                            layer.OffsetSeconds * 1000.0f, remaining);
    }
    if (!layer.Fading && !layer.Offsetting)
        text += ", settled";
    return text;
}
} // namespace

std::vector<PropertyNode> AnimationBlendProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || rig->Layers.empty())
        return { NoteProperty("none", "Blends", "Open a rig to see how its changes blend.") };
    const std::size_t selected = std::min(Workspace.Rig.Navigation.Layer, rig->Layers.size() - 1);
    std::vector<PropertyNode> nodes{ AnimationLayerChoice(*rig, selected) };

    PropertyNode now = GroupProperty("now", "Blending now");
    const AnimPoseState* state = session.SubjectPoseState();
    const AnimPosePool::Slot* slot = session.SubjectPose();
    if (state == nullptr || slot == nullptr)
        now.Children.push_back(NoteProperty("now.none", "Pose", rig->SkeletonPath.empty()
                                                                    ? "The rig names no skeleton, so nothing poses it."
                                                                    : "Nothing posed yet."));
    else
        for (std::size_t l = 0; l < rig->Layers.size() && l < slot->Layers; ++l)
            now.Children.push_back(NoteProperty(std::format("now.{}", l), rig->Layers[l].NameText,
                                                BlendingNow(session, *slot, state->Layers[l], l)));
    nodes.push_back(std::move(now));

    if (const std::optional<AnimationBlendspaceView> space = AnimationBlendspaceOn(session, selected))
    {
        PropertyNode group = GroupProperty("blendspace", "Blendspace");
        group.Descriptor.Tooltip = "Drag the pad on the stage to set its axes' facts for the next tick.";
        group.Children.push_back(NoteProperty("blendspace.path", "Asset", space->Path));
        std::string at = std::format("{} = {:.2f}", space->AxisFact[0], space->Coordinates[0]);
        if (space->Axes > 1)
            at += std::format(", {} = {:.2f}", space->AxisFact[1], space->Coordinates[1]);
        group.Children.push_back(NoteProperty("blendspace.at", "At", at));
        group.Children.push_back(NoteProperty("blendspace.phase", "Phase", std::format("{:.2f}", space->Phase)));
        for (std::size_t s = 0; s < space->Samples.size(); ++s)
            group.Children.push_back(NoteProperty(std::format("blendspace.samples.{}", s), space->Samples[s].Label,
                                                  std::format("{:.2f}", space->Samples[s].Weight)));
        nodes.push_back(std::move(group));
    }

    PropertyNode recent = GroupProperty("recent", "Recent blends");
    const auto& history = session.History();
    for (auto tick = history.rbegin(); tick != history.rend() && recent.Children.size() < kRecentBlends; ++tick)
        for (auto record = tick->Decisions.rbegin();
             record != tick->Decisions.rend() && recent.Children.size() < kRecentBlends; ++record)
        {
            if (record->Cause != AnimDecisionCause::BlendApplied)
                continue;
            const std::string layer = record->Layer < rig->Layers.size() ? rig->Layers[record->Layer].NameText
                                                                        : std::format("layer {}", record->Layer);
            std::string text = std::format("{} \xE2\x86\x92 {}, {} {:.0f} ms", session.TagName(record->PreviousBehavior),
                                           session.TagName(record->Behavior), AnimBlendModeName(record->Blend),
                                           record->BlendSeconds * 1000.0f);
            if (record->Blend == AnimBlendMode::Inertialize)
                text += std::format(", from {:.3f} away", record->BlendMagnitude);
            if (record->BlendOverridden)
                text += ", pairwise override";
            recent.Children.push_back(NoteProperty(std::format("recent.{}", recent.Children.size()),
                                                   std::format("{} at {}", layer, record->Tick), text));
        }
    if (recent.Children.empty())
        recent.Children.push_back(NoteProperty("recent.none", "Blends", "No change has been blended yet."));
    nodes.push_back(std::move(recent));
    return nodes;
}

PropertyEditResult AnimationBlendProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr || key != "layer")
        return PropertyEditResult::Gone;
    const std::optional<std::size_t> layer = ParseAnimationLayerChoice(*rig, value);
    if (!layer)
        return PropertyEditResult::Refused;
    Workspace.Rig.Navigation.Layer = *layer;
    Workspace.Rig.Navigation.Rule = -1;
    return PropertyEditResult::Applied;
}

std::vector<PropertyNode> AnimationBlendProperties::TakeSource::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr)
        return {};
    std::vector<PropertyNode> nodes;
    PropertyNode overrides = GroupProperty(
        "overrides", std::format("Pairwise overrides, {} bound", rig->BlendOverrides.size()));
    overrides.Descriptor.Tooltip = "Past half the cap a rig warns, past the cap it fails to bind "
                                   "(anim.blend.override_cap).";
    for (std::size_t o = 0; o < rig->BlendOverrides.size(); ++o)
    {
        const AnimBoundBlendOverride& entry = rig->BlendOverrides[o];
        overrides.Children.push_back(NoteProperty(
            std::format("overrides.{}", o),
            std::format("{} \xE2\x86\x92 {}", session.TagName(entry.From), session.TagName(entry.To)),
            std::format("{} {:.0f} ms, {}", AnimBlendModeName(entry.Policy.In), entry.Policy.InMs, entry.DeclaredIn)));
    }
    nodes.push_back(std::move(overrides));

    const AnimationTakeComparison& takes = Workspace.Takes;
    PropertyNode ab = GroupProperty("ab", "A/B");
    ab.Descriptor.Tooltip = "Record A, edit a blend, then replay: B runs the same scenario from tick 0 and is "
                            "compared with A tick by tick. A is drawn as a ghost.";
    ab.Descriptor.Verbs = { { .Id = "record", .Label = "Record A" } };
    if (const AnimationPoseTake* a = takes.A())
    {
        ab.Descriptor.Verbs.push_back({ .Id = "replay", .Label = "Replay B against A" });
        ab.Descriptor.Verbs.push_back({ .Id = "clear", .Label = "Clear" });
        ab.Children.push_back(ToggleProperty("ab.ghost", "Ghost", Workspace.Viewport.ShowGhost));
        if (!a->Ticks.empty())
            ab.Children.push_back(
                NoteProperty("ab.a", "A", std::format("ticks {} to {}", a->Ticks.front(), a->Ticks.back())));
        const AnimationPoseComparison& comparison = takes.Comparison();
        std::string result;
        if (!comparison.Refusal.empty())
            result = comparison.Refusal;
        else if (!comparison.Residuals.empty())
        {
            const auto worst = std::ranges::max_element(comparison.Residuals, {}, &AnimationPoseResidual::Position);
            const SkeletonData* skeleton = Workspace.Rig.Skeleton();
            const auto joint = [&](std::uint32_t index) {
                return skeleton != nullptr && index < skeleton->Joints.size() && !skeleton->Joints[index].Name.empty()
                           ? skeleton->Joints[index].Name
                           : std::format("joint {}", index);
            };
            result = worst->Position <= 0.0f && worst->Rotation <= 0.0f
                         ? std::string("B matches A on every tick.")
                         : std::format("Largest at tick {}: {:.3f} at {}, {:.1f} deg at {}", worst->Tick,
                                       worst->Position, joint(worst->PositionJoint), worst->Rotation * 57.29578f,
                                       joint(worst->RotationJoint));
        }
        if (!result.empty())
            ab.Children.push_back(NoteProperty("ab.result", "B", result));
    }
    nodes.push_back(std::move(ab));
    return nodes;
}

PropertyEditResult AnimationBlendProperties::TakeSource::CommitEdit(std::string_view key, std::string_view value)
{
    const std::optional<bool> on = ParseToggle(value);
    if (key != "ab.ghost" || !on)
        return PropertyEditResult::Refused;
    Workspace.Viewport.ShowGhost = *on;
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationBlendProperties::TakeSource::Act(std::string_view key, std::string_view verb,
                                                            std::string_view)
{
    if (key != "ab")
        return PropertyEditResult::Gone;
    AnimationTakeComparison& takes = Workspace.Takes;
    bool done = true;
    if (verb == "record")
        done = takes.RecordA(Workspace.Rig.Simulation);
    else if (verb == "replay")
        done = takes.ReplayAgainstA(Workspace.Rig.Simulation);
    else if (verb == "clear")
        takes.Clear();
    else
        return PropertyEditResult::Gone;
    return done ? PropertyEditResult::Applied : PropertyEditResult::Refused;
}
