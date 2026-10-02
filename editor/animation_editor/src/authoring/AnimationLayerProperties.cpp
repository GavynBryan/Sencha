#include "authoring/AnimationLayerProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigDocumentEdits.h"
#include "authoring/AnimationRigEdits.h"

#include "properties/SettingProperties.h"

#include <anim/AnimRigData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <charconv>
#include <format>

namespace
{
constexpr std::string_view kLayers = "layers.";
constexpr std::string_view kMask = "mask.";

std::string TagName(const AnimationPreviewSession& session, GameplayTagId tag)
{
    const GameplayTagRegistry* tags = session.Tags();
    return tag.IsValid() && tags != nullptr ? std::string(tags->GetName(tag)) : std::string("(none)");
}

std::uint8_t Bit(std::size_t layer)
{
    return static_cast<std::uint8_t>(1u << layer);
}

std::optional<std::size_t> IndexAfter(std::string_view key, std::string_view prefix)
{
    std::size_t index = 0;
    if (!key.starts_with(prefix)
        || std::from_chars(key.data() + prefix.size(), key.data() + key.size(), index).ec != std::errc{})
        return std::nullopt;
    return index;
}

std::string MaskSummary(const AnimBoundLayer& layer)
{
    if (!layer.Masked())
        return "all joints";
    return std::format("{} of {} joints", std::ranges::count(layer.Mask, std::uint8_t{ 1 }), layer.Mask.size());
}

std::string WeightSource(const AnimBoundRig& rig, std::size_t layer, const AnimationPreviewLayerRecord* record)
{
    const int selector = rig.Layers[layer].Selector;
    if (record == nullptr || selector < 0 || record->WeightRule == kAnimNoRule)
        return "the rig's constant weight";
    const auto& rules = rig.Selectors[static_cast<std::size_t>(selector)].WeightRules;
    return record->WeightRule < rules.size() ? std::format("weight rule '{}'", rules[record->WeightRule].Label)
                                             : std::string("a weight rule of an older binding");
}
} // namespace

std::string AnimationLayerProperties::Title() const
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (Inspecting && rig != nullptr && Workspace.Rig.Navigation.Layer < rig->Layers.size())
        return rig->Layers[Workspace.Rig.Navigation.Layer].NameText;
    return "Layers";
}

bool AnimationLayerProperties::StepBack()
{
    const bool was = Inspecting;
    Inspecting = false;
    return was;
}

std::vector<PropertyNode> AnimationLayerProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || rig->Layers.empty())
        return { NoteProperty("none", "Layers", "Open a rig to see its layers.") };
    if (Inspecting && Workspace.Rig.Navigation.Layer < rig->Layers.size())
        return LayerNodes();

    const AnimationPreviewTickRecord* tick = Workspace.Rig.Inspected();
    const AnimationLayerDisplay& display = Workspace.Viewport.LayerDisplay;
    std::vector<PropertyNode> nodes{ NoteProperty(
        "composed", "Composed", "Top to bottom onto the bind pose. Mute and solo change only the stage.") };
    for (std::size_t l = 0; l < rig->Layers.size() && l < kAnimMaxLayers; ++l)
    {
        const AnimBoundLayer& layer = rig->Layers[l];
        const AnimationPreviewLayerRecord* record = tick != nullptr && l < tick->Layers.size() ? &tick->Layers[l]
                                                                                               : nullptr;
        std::string summary = std::format("{} \xC2\xB7 {:.2f} \xC2\xB7 {}",
                                          layer.Mode == AnimLayerMode::Additive ? "additive" : "override",
                                          record != nullptr ? record->Weight : layer.Weight, MaskSummary(layer));
        if (record != nullptr)
            summary += " \xC2\xB7 " + TagName(session, record->Behavior);
        if (!display.Shows(l))
            summary += " \xC2\xB7 hidden";
        PropertyNode row = NoteProperty(std::format("{}{}", kLayers, l), layer.NameText, summary);
        row.Descriptor.Verbs = { { .Id = "inspect", .Label = "Inspect" },
                                 { .Id = "mute", .Label = (display.Muted & Bit(l)) != 0 ? "Unmute" : "Mute" },
                                 { .Id = "solo", .Label = (display.Soloed & Bit(l)) != 0 ? "Unsolo" : "Solo" } };
        nodes.push_back(std::move(row));
    }
    if (!Workspace.Viewport.Note.empty())
        nodes.push_back(NoteProperty("stage", "Stage", Workspace.Viewport.Note));
    return nodes;
}

std::vector<PropertyNode> AnimationLayerProperties::LayerNodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig& rig = *session.Rig();
    const std::size_t l = Workspace.Rig.Navigation.Layer;
    const AnimBoundLayer& layer = rig.Layers[l];
    const AnimationPreviewTickRecord* tick = Workspace.Rig.Inspected();
    const AnimationPreviewLayerRecord* record = tick != nullptr && l < tick->Layers.size() ? &tick->Layers[l] : nullptr;
    const AnimationLayerDisplay& display = Workspace.Viewport.LayerDisplay;

    std::vector<PropertyNode> nodes;
    nodes.push_back(NoteProperty("mode", "Mode", layer.Mode == AnimLayerMode::Additive ? "Additive" : "Override"));
    nodes.push_back(NoteProperty("weight", "Weight",
                                 std::format("{:.2f}, from {}", record != nullptr ? record->Weight : layer.Weight,
                                             WeightSource(rig, l, record))));
    if (record != nullptr)
        nodes.push_back(NoteProperty(
            "playing", "Playing",
            record->Clip < rig.Contents.size()
                ? std::format("{}, {}", TagName(session, record->Behavior), rig.Contents[record->Clip].Path)
                : TagName(session, record->Behavior)));
    nodes.push_back(ToggleProperty("muted", "Muted", (display.Muted & Bit(l)) != 0, "The stage leaves it out"));
    nodes.push_back(ToggleProperty("soloed", "Soloed", (display.Soloed & Bit(l)) != 0,
                                   "The stage shows only soloed layers"));

    PropertyNode mask = GroupProperty("mask", std::format("Mask, {}", MaskSummary(layer)));
    if (l == 0)
        mask.Children.push_back(NoteProperty("mask.first", "Covers",
                                             "Every joint: the first layer is the pose the others compose onto."));
    else
    {
        const std::vector<AnimMaskOp> steps =
            AnimationMaskSteps(Workspace.Documents, Workspace.DataCache(), Workspace.Rig.Path, l);
        if (steps.empty())
            mask.Children.push_back(NoteProperty("mask.none", "Steps", "None: the layer covers every joint."));
        for (std::size_t s = 0; s < steps.size(); ++s)
        {
            PropertyNode step = NoteProperty(std::format("{}{}", kMask, s), steps[s].Exclude ? "Remove" : "Add",
                                             steps[s].Joint + (steps[s].Subtree ? " and below" : " only"));
            step.Descriptor.Verbs = { { .Id = "remove", .Label = "Remove Step" } };
            mask.Children.push_back(std::move(step));
        }
        mask.Children.push_back(NoteProperty("mask.how", "Adding", "Pick a joint on the stage or in Skeleton (J)."));
        if (!steps.empty())
            mask.Descriptor.Verbs = { { .Id = "clear", .Label = "Clear Mask" } };
    }
    nodes.push_back(std::move(mask));
    return nodes;
}

PropertyEditResult AnimationLayerProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const std::optional<bool> on = ParseToggle(value);
    const std::size_t l = Workspace.Rig.Navigation.Layer;
    if (!on || l >= kAnimMaxLayers)
        return PropertyEditResult::Refused;
    AnimationLayerDisplay& display = Workspace.Viewport.LayerDisplay;
    std::uint8_t& bits = key == "muted" ? display.Muted : display.Soloed;
    bits = *on ? static_cast<std::uint8_t>(bits | Bit(l)) : static_cast<std::uint8_t>(bits & ~Bit(l));
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationLayerProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr)
        return PropertyEditResult::Gone;
    if (const std::optional<std::size_t> l = IndexAfter(key, kLayers); l && *l < rig->Layers.size() && *l < kAnimMaxLayers)
    {
        AnimationLayerDisplay& display = Workspace.Viewport.LayerDisplay;
        if (verb == "inspect")
        {
            Workspace.Rig.Navigation.Layer = *l;
            Workspace.Rig.Navigation.Rule = -1;
            Inspecting = true;
        }
        else if (verb == "mute")
            display.Muted = static_cast<std::uint8_t>(display.Muted ^ Bit(*l));
        else if (verb == "solo")
            display.Soloed = static_cast<std::uint8_t>(display.Soloed ^ Bit(*l));
        else
            return PropertyEditResult::Gone;
        return PropertyEditResult::Applied;
    }
    const std::size_t layer = Workspace.Rig.Navigation.Layer;
    const auto edit = [&](const std::function<bool(JsonValue&)>& change) {
        return EditAnimationRig(Workspace.Documents, Workspace.Rig.Path, change, Workspace.DocumentError)
                   ? PropertyEditResult::Applied
                   : PropertyEditResult::Refused;
    };
    if (key == "mask" && verb == "clear")
        return edit([&](JsonValue& root) { return ClearAnimMask(root, layer); });
    if (const std::optional<std::size_t> step = IndexAfter(key, kMask); step && verb == "remove")
        return edit([&](JsonValue& root) { return RemoveAnimMaskStep(root, layer, *step); });
    return PropertyEditResult::Gone;
}
