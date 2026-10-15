#include "authoring/AbilityPreviewInspector.h"

#include "authoring/AbilityElements.h"
#include "authoring/AbilityPreviewSession.h"
#include "properties/SettingProperties.h"

#include <abilities/AbilityDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <bit>
#include <format>

namespace
{
std::string_view EventName(AbilityTraceEvent event)
{
    switch (event)
    {
    case AbilityTraceEvent::Refused: return "Refused";
    case AbilityTraceEvent::Started: return "Started";
    case AbilityTraceEvent::Ended: return "Ended";
    case AbilityTraceEvent::Transition: return "Transition";
    case AbilityTraceEvent::Skipped: return "Skipped";
    case AbilityTraceEvent::Operated: return "Ran";
    case AbilityTraceEvent::Cue: return "Cue";
    case AbilityTraceEvent::BindingAmbiguous: return "Ambiguous";
    case AbilityTraceEvent::Blocked: return "Blocked";
    case AbilityTraceEvent::AlreadyCommitted: return "Already paid";
    }
    return "Event";
}

// An element of the running definition by its label in the working data, or its key.
std::string Named(const JsonValue* data, const std::string& key)
{
    if (data == nullptr || key.empty())
        return key;
    const std::optional<AbilityElement> element = FindAbilityElement(*data, key);
    return element ? AbilityElementLabel(*data, *element) : key;
}

struct Names
{
    const AbilityDefinition* Definition;
    const JsonValue* Data;

    [[nodiscard]] std::string Phase(std::uint8_t index) const
    {
        return Definition != nullptr && index < Definition->Phases.size()
                 ? Named(Data, Definition->Phases[index].Key)
                 : std::format("phase {}", index);
    }
    [[nodiscard]] std::string Op(std::uint16_t index) const
    {
        return Definition != nullptr && index < Definition->Ops.size() ? Named(Data, Definition->Ops[index].Key)
                                                                       : std::string{};
    }
    [[nodiscard]] std::string Condition(std::uint16_t index) const
    {
        return Definition != nullptr && index < Definition->Conditions.size()
                 ? Named(Data, Definition->Conditions[index].Key)
                 : std::string{};
    }
};

std::string Describe(const AbilityTraceRecord& record, const Names& names, const GameplayTagRegistry* tags)
{
    switch (record.Event)
    {
    case AbilityTraceEvent::Refused:
    {
        const std::string condition = names.Condition(record.Element);
        return condition.empty() ? std::string(AbilityRefusalName(record.Refusal))
                                 : std::format("{}: {}", AbilityRefusalName(record.Refusal), condition);
    }
    case AbilityTraceEvent::Ended:
    {
        const std::string because = record.Element != 0xFFFF ? names.Condition(record.Element) : std::string{};
        return because.empty() ? std::string(AbilityEndReasonName(record.EndReason))
                               : std::format("{}: {}", AbilityEndReasonName(record.EndReason), because);
    }
    case AbilityTraceEvent::Transition: return std::format("\xE2\x86\x92 {}", names.Phase(record.Phase));
    case AbilityTraceEvent::Operated:
        return std::format("{}: {}", names.Op(record.Element), VerbAdmissionName(record.Admission));
    case AbilityTraceEvent::Skipped: return names.Op(record.Element);
    case AbilityTraceEvent::Cue:
        return tags != nullptr && record.Cue.IsValid() ? std::string(tags->GetName(record.Cue)) : std::string("cue");
    default: return {};
    }
}
} // namespace

std::vector<PropertyNode> AbilityPreviewInspector::Nodes() const
{
    const std::deque<AbilityPreviewTickRecord>& history = Preview.History();
    const auto found = std::ranges::find(history, Preview.Tick(), &AbilityPreviewTickRecord::Tick);
    if (found == history.end())
        return {};
    const AbilityPreviewTickRecord& tick = *found;
    const Names names{ Preview.Definition(), Data ? Data() : nullptr };
    const World* world = Preview.SimulationWorld();
    const GameplayTagRegistry* tags = world != nullptr ? world->TryGetResource<GameplayTagRegistry>() : nullptr;

    std::vector<PropertyNode> nodes;
    nodes.push_back(NoteProperty("tick", "Tick",
                                 std::format("{} \xC2\xB7 time \xC3\x97{:.2f}", tick.Tick, ActorScaleValue(tick.ScaleQ16))));
    for (const AbilityRecord& record : tick.Records)
    {
        PropertyNode activation =
            GroupProperty(std::format("activation.{}", record.Sequence),
                          std::format("Activation {}{}", record.Sequence,
                                      record.State == AbilityRecordState::Lingering ? " (ended)" : ""));
        activation.Children.push_back(NoteProperty(std::format("activation.{}.phase", record.Sequence), "Phase",
                                                   names.Phase(record.Phase)));
        activation.Children.push_back(NoteProperty(std::format("activation.{}.pulse", record.Sequence), "Pulse",
                                                   std::format("{}", record.Pulse)));
        activation.Children.push_back(NoteProperty(std::format("activation.{}.leases", record.Sequence), "Leases held",
                                                   std::format("{}", record.LeaseCount)));
        activation.Children.push_back(NoteProperty(std::format("activation.{}.paid", record.Sequence), "Groups paid",
                                                   std::format("{}", std::popcount(record.PaidGroups))));
        activation.Children.push_back(NoteProperty(std::format("activation.{}.cooldown", record.Sequence), "Cooldown",
                                                   record.CooldownStarted ? "started" : "not started"));
        if (record.State == AbilityRecordState::Lingering)
            activation.Children.push_back(NoteProperty(std::format("activation.{}.ended", record.Sequence), "Ended",
                                                       std::string(AbilityEndReasonName(record.EndReason))));
        nodes.push_back(std::move(activation));
    }
    for (std::size_t i = 0; i < tick.Trace.size(); ++i)
        nodes.push_back(NoteProperty(std::format("event.{}", i), std::string(EventName(tick.Trace[i].Event)),
                                     Describe(tick.Trace[i], names, tags)));
    return nodes;
}
