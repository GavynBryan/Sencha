#include "authoring/AnimationFlowProperties.h"

#include "authoring/AnimationLayerChoice.h"
#include "authoring/AnimationPredicateText.h"
#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimFlowData.h>

#include <algorithm>
#include <format>

namespace
{
// The flow the layer plays on the inspected tick, and the layer's record there.
struct Playing
{
    const AnimBoundFlow* Flow = nullptr;
    const AnimationPreviewLayerRecord* Layer = nullptr;
    AnimTick Tick = 0;
};

Playing PlayingOn(const AnimationPreviewWorkspace& workspace, const AnimBoundRig& rig, std::size_t layer)
{
    const AnimationPreviewTickRecord* record = workspace.Rig.Inspected();
    if (record == nullptr || layer >= record->Layers.size())
        return {};
    const AnimationPreviewLayerRecord& at = record->Layers[layer];
    const int flow = at.Content < rig.Contents.size() ? rig.Contents[at.Content].Flow : -1;
    return flow >= 0 ? Playing{ &rig.Flows[static_cast<std::size_t>(flow)], &at, record->Tick } : Playing{};
}

std::string HowLeft(const AnimFlowData& flow, std::size_t s)
{
    const AnimFlowSectionDecl& section = flow.Sections[s];
    std::string text;
    if (section.Loop == AnimFlowLoop::While)
        text += std::format("repeats while {}; ", DescribeAnimPredicate(section.While));
    else if (section.Loop == AnimFlowLoop::Count)
        text += std::format("plays {}.{} times; ", section.CountIntent, section.CountParam);
    for (const AnimFlowBranchDecl& branch : section.Branches)
        text += std::format("to {} when {}; ", branch.To, DescribeAnimPredicate(branch.When));
    text += section.Ends ? "otherwise ends the flow" : s + 1 < flow.Sections.size() ? "otherwise next" : "then ends";
    if (section.CancelTiming == AnimCancelTiming::Immediate)
        text += "; a cancel leaves at once";
    return text;
}
} // namespace

std::string AnimationFlowProperties::Title() const
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    const std::size_t layer = Workspace.Rig.Navigation.Layer;
    if (rig == nullptr || layer >= rig->Layers.size())
        return "Flow";
    return "Flow on " + rig->Layers[layer].NameText;
}

std::vector<PropertyNode> AnimationFlowProperties::Nodes() const
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr || rig->Layers.empty())
        return { NoteProperty("none", "Flow", "Run a rig to see the flow its layers play.") };
    const std::size_t layer = std::min(Workspace.Rig.Navigation.Layer, rig->Layers.size() - 1);
    std::vector<PropertyNode> nodes{ AnimationLayerChoice(*rig, layer) };
    const Playing playing = PlayingOn(Workspace, *rig, layer);
    if (playing.Flow == nullptr)
    {
        nodes.push_back(NoteProperty("not_playing", "Flow", "It is not playing a flow."));
        return nodes;
    }
    const AnimBoundFlow& flow = *playing.Flow;
    const AnimLayerFlow& state = playing.Layer->Flow;
    PropertyNode path = NoteProperty("flow", "Flow", flow.Path);
    path.Descriptor.Verbs = { { .Id = "open", .Label = "Edit" } };
    nodes.push_back(std::move(path));

    const std::string section = state.Section < flow.Sections.size() ? flow.Sections[state.Section].TagName
                                                                     : std::string("?");
    const double seconds = static_cast<double>(playing.Tick - std::min(playing.Tick, state.SectionStartTick))
                           * Workspace.Rig.Simulation.TickSeconds();
    nodes.push_back(NoteProperty(
        "now", "Now",
        state.Phase == AnimFlowPhase::Complete
            ? std::string("Complete, holding the last pose.")
            : std::format("{}, {:.2f}s in{}", section, seconds,
                          state.LoopCount > 0 ? std::format(", pass {}", state.LoopCount + 1) : std::string())));
    nodes.push_back(NoteProperty("cancel", "A cancel",
                                 flow.Cancel >= 0
                                     ? "goes to " + flow.Sections[static_cast<std::size_t>(flow.Cancel)].TagName
                                     : std::string("ends the flow")));

    const DataAssetCache& data = Workspace.DataCache();
    if (const AnimFlowData* authored = data.TryGet<AnimFlowData>(data.Find(flow.Path), kAnimFlowType))
    {
        PropertyNode sections = GroupProperty("sections", "How each section is left");
        sections.Descriptor.Tooltip = "Control only goes forward: a sequence that needs to go back is a decision "
                                      "for gameplay, answered with a new request.";
        for (std::size_t s = 0; s < authored->Sections.size(); ++s)
            sections.Children.push_back(NoteProperty(
                std::format("sections.{}", s),
                authored->Sections[s].Tag + (s == state.Section && state.Phase == AnimFlowPhase::Playing ? ", playing" : ""),
                HowLeft(*authored, s)));
        nodes.push_back(std::move(sections));
    }
    return nodes;
}

PropertyEditResult AnimationFlowProperties::CommitEdit(std::string_view key, std::string_view value)
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

PropertyEditResult AnimationFlowProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr || key != "flow" || verb != "open")
        return PropertyEditResult::Gone;
    const Playing playing = PlayingOn(Workspace, *rig, Workspace.Rig.Navigation.Layer);
    if (playing.Flow == nullptr)
        return PropertyEditResult::Gone;
    Open(playing.Flow->Path);
    return PropertyEditResult::Applied;
}
