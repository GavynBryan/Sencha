#include "authoring/AnimationRequestProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimRequests.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <array>
#include <charconv>
#include <format>

namespace
{
constexpr std::array<std::string_view, 3> kLifetimes{ "Held", "Fixed", "Impulse" };
constexpr std::array<std::string_view, 4> kReasons{ "Released", "Interrupted", "Failed", "Superseded" };
constexpr std::string_view kLayers = "issue.layers.";
constexpr std::string_view kParams = "issue.params.";
constexpr std::string_view kLive = "live.";

std::string IntentName(const AnimationPreviewSession& session, GameplayTagId intent)
{
    if (const AnimBoundRig* rig = session.Rig())
        if (const AnimBoundIntent* bound = rig->FindIntent(intent))
            return bound->Name;
    const GameplayTagRegistry* tags = session.Tags();
    return intent.IsValid() && tags != nullptr ? std::string(tags->GetName(intent)) : std::string("(none)");
}

std::optional<std::size_t> IndexAfter(std::string_view key, std::string_view prefix)
{
    std::size_t index = 0;
    if (!key.starts_with(prefix)
        || std::from_chars(key.data() + prefix.size(), key.data() + key.size(), index).ec != std::errc{})
        return std::nullopt;
    return index;
}

} // namespace

std::vector<PropertyNode> AnimationRequestProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (session.Rig() == nullptr)
        return { NoteProperty("none", "Requests", "Open a rig to issue requests against it.") };
    std::vector<PropertyNode> nodes{ IssueNodes(), LiveNodes() };

    if (!session.History().empty())
    {
        const AnimationPreviewTickRecord& record = session.History().back();
        PropertyNode outcomes = GroupProperty("outcomes", std::format("On tick {}", record.Tick));
        for (const AnimationPreviewActionOutcome& outcome : record.Actions)
        {
            if (outcome.Kind != AnimationScenarioActionKind::IssueRequest
                && outcome.Kind != AnimationScenarioActionKind::CancelRequest)
                continue;
            std::string text;
            if (!outcome.Problem.empty())
                text = outcome.Problem;
            else if (outcome.Kind == AnimationScenarioActionKind::IssueRequest && !outcome.Request.Accepted())
                text = std::format("refused ({})", AnimRejectReasonName(outcome.Request.Reject));
            else if (outcome.Request.Status == AnimRequestStatus::Superseded)
                text = "superseded the source's previous request";
            else if (outcome.Request.Status == AnimRequestStatus::Deduplicated)
                text = "already issued this tick";
            if (!text.empty())
                outcomes.Children.push_back(
                    NoteProperty(std::format("outcomes.{}", outcomes.Children.size()), outcome.Subject, text));
        }
        if (!outcomes.Children.empty())
            nodes.push_back(std::move(outcomes));
    }
    return nodes;
}

PropertyNode AnimationRequestProperties::IssueNodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig& rig = *session.Rig();
    PropertyNode issue = GroupProperty("issue", "Issue a request");
    const std::vector<std::string>& participants = session.Scenario().Participants;
    if (participants.empty())
    {
        issue.Children.push_back(NoteProperty("issue.none", "Source", "Add a participant to issue requests from."));
        return issue;
    }
    std::vector<std::string_view> sources(participants.begin(), participants.end());
    issue.Children.push_back(ChoiceProperty("issue.source", "Source", sources, std::min(Participant, sources.size() - 1)));

    const AnimBoundIntent* intent = nullptr;
    if (rig.HasRequestSchema)
    {
        if (rig.Intents.empty())
        {
            issue.Children.push_back(
                NoteProperty("issue.intent", "Intent", "The rig's request schema declares no intents that resolve."));
            return issue;
        }
        std::vector<std::string_view> intents;
        for (const AnimBoundIntent& bound : rig.Intents)
            intents.push_back(bound.Name);
        const std::size_t chosen = std::min(Intent, intents.size() - 1);
        issue.Children.push_back(ChoiceProperty("issue.intent", "Intent", intents, chosen));
        intent = &rig.Intents[chosen];
    }
    else
        issue.Children.push_back(TextProperty("issue.intent", "Intent tag", FreeIntent));

    issue.Children.push_back(ChoiceProperty("issue.lifetime", "Lifetime", kLifetimes, static_cast<std::size_t>(Lifetime)));
    if (Lifetime == AnimRequestLifetime::Fixed)
        issue.Children.push_back(NumberProperty("issue.ticks", "Ticks", FixedTicks, 1.0, -1.0, 1.0));
    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
        issue.Children.push_back(ToggleProperty(std::format("{}{}", kLayers, l), rig.Layers[l].NameText,
                                                ((Layers >> l) & 1u) != 0));
    if (intent != nullptr)
        for (std::size_t p = 0; p < intent->Params.size(); ++p)
        {
            const AnimBoundParam& param = intent->Params[p];
            const AnimationScenarioValue value = p < Params.size() ? Params[p] : AnimationScenarioValue{};
            const std::string key = std::format("{}{}", kParams, p);
            switch (param.Kind)
            {
            case AnimRequestParamKind::Bool: issue.Children.push_back(ToggleProperty(key, param.Name, value.Bool)); break;
            case AnimRequestParamKind::Tag:  issue.Children.push_back(TextProperty(key, param.Name, value.Name)); break;
            default:
                issue.Children.push_back(NumberProperty(key, param.Name, value.Number, 0.0, -1.0,
                                                        param.Kind == AnimRequestParamKind::Int ? 1.0 : 0.0));
                break;
            }
        }
    issue.Descriptor.Verbs = { { .Id = "issue", .Label = "Issue on Next Tick" } };
    return issue;
}

PropertyNode AnimationRequestProperties::LiveNodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimRequestSet* set = session.Requests();
    std::size_t occupied = 0;
    if (set != nullptr)
        for (const AnimRequest& request : set->Records)
            occupied += request.Occupied ? 1 : 0;
    PropertyNode live = GroupProperty("live", std::format("Records, {} of {}", occupied, kAnimRequestCapacity));
    live.Descriptor.Tooltip = "A full set refuses; nothing is evicted.";
    if (set == nullptr)
        return live;
    const AnimTick now = session.Tick();
    for (const AnimRequest& request : set->Records)
    {
        if (!request.Occupied)
            continue;
        const std::string source(session.ParticipantName(request.Id.Source));
        const std::string intent = IntentName(session, request.Intent);
        std::string status;
        if (request.IsCancelled())
            status = std::format("{} at {}, kept to {}", AnimCancelReasonName(request.CancelReason), request.CancelTick,
                                 request.TailUntilTick);
        else if (!IsAnimRequestLive(request, now))
            status = "expired";
        else
            status = FindPrimaryAnimRequest(*set, request.Intent, now) == &request ? "live, primary" : "live";
        PropertyNode row = NoteProperty(
            std::format("{}{}", kLive, request.Id.Sequence), std::format("{} from {}", intent, source),
            std::format("{}, age {}, layers 0x{:02X}, {}", AnimRequestLifetimeName(request.Lifetime),
                        now - request.StartTick, request.Layers, status));
        if (IsAnimRequestLive(request, now) && !source.empty())
            row.Descriptor.Verbs = { { .Id = "cancel",
                                       .Label = "Cancel",
                                       .Choices = { kReasons.begin(), kReasons.end() } } };
        live.Children.push_back(std::move(row));
    }
    return live;
}

PropertyEditResult AnimationRequestProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr)
        return PropertyEditResult::Gone;
    if (key == "issue.source")
    {
        const std::vector<std::string>& participants = session.Scenario().Participants;
        const std::vector<std::string_view> sources(participants.begin(), participants.end());
        const std::optional<std::size_t> source = ParseChoice(value, sources);
        if (!source)
            return PropertyEditResult::Refused;
        Participant = *source;
        return PropertyEditResult::Applied;
    }
    if (key == "issue.intent")
    {
        if (!rig->HasRequestSchema)
        {
            FreeIntent = value;
            return PropertyEditResult::Applied;
        }
        std::vector<std::string_view> intents;
        for (const AnimBoundIntent& bound : rig->Intents)
            intents.push_back(bound.Name);
        const std::optional<std::size_t> intent = ParseChoice(value, intents);
        if (!intent)
            return PropertyEditResult::Refused;
        Intent = *intent;
        Params.clear();
        return PropertyEditResult::Applied;
    }
    if (key == "issue.lifetime")
    {
        const std::optional<std::size_t> lifetime = ParseChoice(value, kLifetimes);
        if (!lifetime)
            return PropertyEditResult::Refused;
        Lifetime = static_cast<AnimRequestLifetime>(*lifetime);
        return PropertyEditResult::Applied;
    }
    if (key == "issue.ticks")
    {
        const std::optional<double> ticks = ParseNumber(value, 1.0, -1.0);
        if (!ticks)
            return PropertyEditResult::Refused;
        FixedTicks = static_cast<std::uint32_t>(std::max(1.0, *ticks));
        return PropertyEditResult::Applied;
    }
    if (const std::optional<std::size_t> layer = IndexAfter(key, kLayers); layer && *layer < kAnimMaxLayers)
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on)
            return PropertyEditResult::Refused;
        const auto bit = static_cast<std::uint8_t>(1u << *layer);
        Layers = *on ? static_cast<std::uint8_t>(Layers | bit) : static_cast<std::uint8_t>(Layers & ~bit);
        return PropertyEditResult::Applied;
    }
    const std::optional<std::size_t> param = IndexAfter(key, kParams);
    const AnimBoundIntent* intent = rig->HasRequestSchema && Intent < rig->Intents.size() ? &rig->Intents[Intent] : nullptr;
    if (!param || intent == nullptr || *param >= intent->Params.size())
        return PropertyEditResult::Gone;
    Params.resize(intent->Params.size());
    switch (intent->Params[*param].Kind)
    {
    case AnimRequestParamKind::Bool:
        if (const std::optional<bool> on = ParseToggle(value))
            Params[*param] = AnimationScenarioValue::FromBool(*on);
        else
            return PropertyEditResult::Refused;
        break;
    case AnimRequestParamKind::Tag: Params[*param] = AnimationScenarioValue::FromName(std::string(value)); break;
    default:
        if (const std::optional<double> number = ParseNumber(value))
            Params[*param] = AnimationScenarioValue::FromNumber(*number);
        else
            return PropertyEditResult::Refused;
        break;
    }
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationRequestProperties::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr)
        return PropertyEditResult::Gone;
    if (key == "issue" && verb == "issue")
    {
        const std::vector<std::string>& participants = session.Scenario().Participants;
        if (participants.empty())
            return PropertyEditResult::Refused;
        const AnimBoundIntent* intent = rig->HasRequestSchema && !rig->Intents.empty()
                                            ? &rig->Intents[std::min(Intent, rig->Intents.size() - 1)]
                                            : nullptr;
        AnimationScenarioAction action;
        action.Participant = participants[std::min(Participant, participants.size() - 1)];
        action.Intent = intent != nullptr ? intent->Name : FreeIntent;
        action.Lifetime = Lifetime;
        action.FixedTicks = FixedTicks;
        action.Layers = Layers;
        if (intent != nullptr)
            for (std::size_t p = 0; p < intent->Params.size(); ++p)
                action.Params.emplace_back(intent->Params[p].Name,
                                           p < Params.size() ? Params[p] : AnimationScenarioValue{});
        if (action.Intent.empty())
            return PropertyEditResult::Refused;
        session.IssueRequest(std::move(action));
        return PropertyEditResult::Applied;
    }
    const std::optional<std::size_t> sequence = IndexAfter(key, kLive);
    const std::optional<std::size_t> reason = ParseChoice(choice, kReasons);
    const AnimRequestSet* set = session.Requests();
    if (!sequence || verb != "cancel" || !reason || set == nullptr)
        return PropertyEditResult::Gone;
    for (const AnimRequest& request : set->Records)
        if (request.Occupied && request.Id.Sequence == *sequence)
        {
            session.CancelRequest(std::string(session.ParticipantName(request.Id.Source)),
                                  IntentName(session, request.Intent), static_cast<AnimCancelReason>(*reason + 1));
            return PropertyEditResult::Applied;
        }
    return PropertyEditResult::Gone;
}
