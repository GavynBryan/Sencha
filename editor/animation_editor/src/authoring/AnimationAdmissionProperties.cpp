#include "authoring/AnimationAdmissionProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimationClipCache.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <array>
#include <format>

namespace
{
constexpr std::array<std::string_view, 2> kRoles{ "Authority", "Client" };
constexpr std::string_view kRecorders = "recorders.";
// The latest this many crossings, lifecycle events and recorded invocations.
constexpr std::size_t kShown = 40;

std::string EventName(const AnimationPreviewWorkspace& workspace, const AnimDecisionRecord& record)
{
    const AnimBoundRig* rig = workspace.Rig.Simulation.Rig();
    if (rig == nullptr || record.Content >= rig->Contents.size())
        return std::format("{}", record.EventKey);
    if (const AnimationClipData* clip = workspace.Clips().Get(rig->Contents[record.Content].Clip))
        for (const AnimationClipEvent& event : clip->Events)
            if (event.Key == record.EventKey)
                return std::format("{} ({})", event.Name.empty() ? std::to_string(record.EventKey) : event.Name,
                                   event.Binding);
    return std::format("{}", record.EventKey);
}
} // namespace

std::vector<PropertyNode> AnimationAdmissionProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (!session.IsOpen())
        return { NoteProperty("admissions.none", "Admissions", "Open a rig to simulate it.") };
    std::vector<PropertyNode> nodes{ ChoiceProperty(
        "role", "Preview as", kRoles, session.Scenario().Role == AnimationPreviewRole::Client ? 1 : 0,
        "Only an authority produces gameplay events") };

    PropertyNode recorders = GroupProperty("recorders", "Recorders");
    recorders.Descriptor.Tooltip = "Nothing runs the game here. A verb without a recorder answers Unavailable; a "
                                   "recorder accepts and keeps what it was handed, labelled as a recorder's.";
    if (const VerbRegistry* verbs = session.Verbs())
        for (const VerbId id : verbs->Live())
            if (const VerbDefinition* definition = verbs->Get(id))
                recorders.Children.push_back(ToggleProperty(std::string(kRecorders) + definition->Name,
                                                            definition->Name, session.HasRecorder(definition->Name)));
    nodes.push_back(std::move(recorders));

    PropertyNode admitted = GroupProperty("admitted", "Latest events");
    const auto& history = session.History();
    for (auto tick = history.rbegin(); tick != history.rend() && admitted.Children.size() < kShown; ++tick)
    {
        for (const AnimDecisionRecord& record : tick->Decisions)
        {
            const bool event = record.Cause == AnimDecisionCause::EventCrossed;
            if (!event && record.Cause != AnimDecisionCause::BehaviorEntered
                && record.Cause != AnimDecisionCause::BehaviorExited)
                continue;
            const std::string what = event ? EventName(Workspace, record) : session.TagName(record.Behavior);
            admitted.Children.push_back(NoteProperty(
                std::format("admitted.{}", admitted.Children.size()),
                std::format("{} at {}", AnimDecisionCauseName(record.Cause), record.Tick),
                std::format("{}: {}{}", what, AnimEventOutcomeName(record.EventOutcome),
                            record.EventOutcome == AnimEventOutcome::Fired
                                ? std::format(", {}", VerbAdmissionName(record.Admission))
                                : std::string())));
        }
        for (const AnimationPreviewInvocation& invocation : tick->Invocations)
        {
            std::string arguments;
            for (const auto& [name, value] : invocation.Arguments)
                arguments += std::format("{}{}={}", arguments.empty() ? "" : ", ", name, value);
            admitted.Children.push_back(NoteProperty(
                std::format("admitted.{}", admitted.Children.size()), std::format("recorder at {}", invocation.Tick),
                std::format("{}({}) via {}, by {}{}{}", invocation.Verb, arguments, invocation.Binding,
                            invocation.Producer, invocation.Instigator.empty() ? "" : " for ", invocation.Instigator)));
        }
    }
    if (admitted.Children.empty())
        admitted.Children.push_back(NoteProperty("admitted.none", "Events", "None has been crossed yet."));
    nodes.push_back(std::move(admitted));
    return nodes;
}

bool AnimationAdmissionProperties::BeginEdit(std::string_view key)
{
    return key == "role" || key.starts_with(kRecorders);
}

PropertyEditResult AnimationAdmissionProperties::CommitEdit(std::string_view key, std::string_view value)
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (key == "role")
    {
        const std::optional<std::size_t> role = ParseChoice(value, kRoles);
        if (!role)
            return PropertyEditResult::Refused;
        session.SetRole(*role == 1 ? AnimationPreviewRole::Client : AnimationPreviewRole::Authority);
        return PropertyEditResult::Applied;
    }
    const std::optional<bool> on = ParseToggle(value);
    if (!key.starts_with(kRecorders) || !on)
        return PropertyEditResult::Refused;
    session.SetRecorder(key.substr(kRecorders.size()), *on);
    return PropertyEditResult::Applied;
}
