#include "authoring/AnimationFactProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimFactEvaluation.h>
#include <anim/AnimFacts.h>

#include <cmath>
#include <format>

namespace
{
constexpr std::string_view kFacts = "facts.";

std::string FormatFact(const AnimationPreviewSession& session, AnimFactKind kind, std::uint32_t bits)
{
    switch (kind)
    {
    case AnimFactKind::Bool:   return AnimFactToBool(bits) ? "true" : "false";
    case AnimFactKind::Float:  return std::format("{:.3f}", AnimFactToFloat(bits));
    case AnimFactKind::Int:    return std::format("{}", AnimFactToInt(bits));
    case AnimFactKind::Tag:    return session.TagName(GameplayTagId{ bits });
    case AnimFactKind::TagSet: return AnimFactToBool(bits) ? "has tags" : "no container";
    }
    return {};
}

std::string DescribeDerivation(const AnimBoundRig& rig, const AnimBoundDerivation& derivation)
{
    std::string sources;
    for (std::size_t i = 0; i < derivation.SourceCount; ++i)
        sources += std::format("{}{}{}", i > 0 ? ", " : "", derivation.Sources[i].Negate ? "not " : "",
                               rig.Slots[derivation.Sources[i].Slot].Name);
    std::string text = std::format("{}({})", AnimDerivationOpName(derivation.Op), sources);
    if (derivation.WindowMs > 0.0f)
        text += std::format(" over {:.0f} ms", derivation.WindowMs);
    return text;
}

const AnimBoundFactSlot* SlotOf(const AnimBoundRig& rig, std::string_view key)
{
    if (!key.starts_with(kFacts))
        return nullptr;
    const int slot = rig.FindSlot(std::string(key.substr(kFacts.size())));
    return slot >= 0 ? &rig.Slots[static_cast<std::size_t>(slot)] : nullptr;
}
} // namespace

std::vector<PropertyNode> AnimationFactProperties::Nodes() const
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || !rig->HasFacts)
        return { NoteProperty("none", "Facts", rig == nullptr ? "Open a rig to drive its facts."
                                                              : "This rig carries no facts: it is a Prop rig.") };
    if (!rig->Valid)
        return { NoteProperty("none", "Facts", "The rig does not bind; see Problems. Its entity gathers nothing.") };

    std::vector<PropertyNode> nodes{ NoteProperty(
        "exact", "Derived",
        session.FactsExact() ? std::string("Exact.")
                             : std::format("Warming up: exact after {:.0f} ms of observation.", rig->HorizonMs)) };
    const std::vector<std::uint32_t> next =
        session.IsPlaying() ? std::vector<std::uint32_t>{} : session.PreviewNextTick();
    const std::span<const std::uint32_t> facts = session.Facts();
    for (std::size_t i = 0; i < rig->Slots.size() && i < facts.size(); ++i)
    {
        const AnimBoundFactSlot& slot = rig->Slots[i];
        const std::string key = std::string(kFacts) + slot.Name;
        std::string tooltip = std::format("{}{}, declared in {}", AnimFactKindName(slot.Kind),
                                          slot.Local ? ", local" : "", slot.DeclaredIn);
        if (i < next.size() && next[i] != facts[i])
            tooltip += "; next tick " + FormatFact(session, slot.Kind, next[i]);
        PropertyNode node;
        if (slot.Derivation >= 0)
            node = NoteProperty(key, slot.Name,
                                std::format("{} = {}", FormatFact(session, slot.Kind, facts[i]),
                                            DescribeDerivation(*rig, rig->Derivations[static_cast<std::size_t>(slot.Derivation)])));
        else
            switch (slot.Kind)
            {
            case AnimFactKind::Bool:  node = ToggleProperty(key, slot.Name, AnimFactToBool(facts[i])); break;
            case AnimFactKind::Float:
            case AnimFactKind::Int:
                node = NumberProperty(key, slot.Name, AnimFactToNumber(slot.Kind, facts[i]), 0.0, -1.0,
                                      slot.Kind == AnimFactKind::Int ? 1.0 : 0.0);
                break;
            case AnimFactKind::Tag:   node = TextProperty(key, slot.Name, FormatFact(session, slot.Kind, facts[i])); break;
            case AnimFactKind::TagSet:
                node = NoteProperty(key, slot.Name, "A tag container; scenarios do not drive one yet.");
                break;
            }
        node.Descriptor.Tooltip = std::move(tooltip);
        if (slot.Derivation < 0 && session.Input(slot.Name) != nullptr)
            node.Descriptor.Verbs.push_back({ .Id = "clear", .Label = "Clear" });
        if (slot.Kind != AnimFactKind::TagSet)
            node.Descriptor.Verbs.push_back(
                { .Id = "plot", .Label = Plot == slot.Name ? std::string("Plotted") : std::string("Plot") });
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool AnimationFactProperties::BeginEdit(std::string_view key)
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    const AnimBoundFactSlot* slot = rig != nullptr ? SlotOf(*rig, key) : nullptr;
    return slot != nullptr && slot->Derivation < 0 && slot->Kind != AnimFactKind::TagSet;
}

PropertyEditResult AnimationFactProperties::CommitEdit(std::string_view key, std::string_view value)
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    const AnimBoundFactSlot* slot = rig != nullptr ? SlotOf(*rig, key) : nullptr;
    if (slot == nullptr || slot->Derivation >= 0)
        return PropertyEditResult::Gone;
    switch (slot->Kind)
    {
    case AnimFactKind::Bool:
        if (const std::optional<bool> on = ParseToggle(value))
        {
            session.SetFact(slot->Name, AnimationScenarioValue::FromBool(*on));
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Refused;
    case AnimFactKind::Float:
    case AnimFactKind::Int:
        if (const std::optional<double> number = ParseNumber(value))
        {
            session.SetFact(slot->Name, AnimationScenarioValue::FromNumber(
                                            slot->Kind == AnimFactKind::Int ? std::round(*number) : *number));
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Refused;
    case AnimFactKind::Tag:
        session.SetFact(slot->Name, AnimationScenarioValue::FromName(std::string(value)));
        return PropertyEditResult::Applied;
    case AnimFactKind::TagSet: return PropertyEditResult::Refused;
    }
    return PropertyEditResult::Gone;
}

PropertyEditResult AnimationFactProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    const AnimBoundFactSlot* slot = rig != nullptr ? SlotOf(*rig, key) : nullptr;
    if (slot == nullptr)
        return PropertyEditResult::Gone;
    if (verb == "clear")
        session.ClearFact(slot->Name);
    else if (verb == "plot")
        Plot = Plot == slot->Name ? std::string() : slot->Name;
    else
        return PropertyEditResult::Gone;
    return PropertyEditResult::Applied;
}
