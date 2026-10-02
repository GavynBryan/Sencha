#include "authoring/AnimationLabProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationSessionLab.h"
#include "authoring/AnimationTimelineLanes.h"

#include "properties/SettingProperties.h"

#include <anim/AnimDecisionLog.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
constexpr std::string_view kGuesses = "guesses.";

std::string TagName(const AnimationPreviewSession& session, GameplayTagId tag)
{
    const GameplayTagRegistry* tags = session.Tags();
    return tag.IsValid() && tags != nullptr ? std::string(tags->GetName(tag)) : std::string("(none)");
}

const AnimationPreviewTickRecord* RecordAt(const AnimationPreviewSession& session, AnimTick tick)
{
    const auto& history = session.History();
    if (history.empty() || tick < history.front().Tick || tick > history.back().Tick)
        return nullptr;
    return &history[static_cast<std::size_t>(tick - history.front().Tick)];
}

std::string RequestText(const AnimationPreviewSession& session, const AnimRequest& request)
{
    std::string text = std::format("#{} {} from {}", request.Id.Sequence, TagName(session, request.Intent),
                                   request.StartTick);
    if (request.AnchorSection != kAnimNoAnchorSection)
        text += std::format(", section {} from {}", request.AnchorSection, request.AnchorSectionStartTick);
    if (request.IsCancelled())
        text += std::format(", cancelled at {}, tail to {}", request.CancelTick, request.TailUntilTick);
    if (request.Predicted)
        text += ", guessed";
    return text;
}

PropertyNode Machine(std::string key, std::string label, const AnimationPreviewSession& session,
                     const AnimationPreviewTickRecord& record)
{
    PropertyNode machine = GroupProperty(key, std::move(label));
    for (std::size_t l = 0; l < record.Layers.size(); ++l)
    {
        const AnimationPreviewLayerRecord& layer = record.Layers[l];
        std::string text = std::format("{}, {:.3f} s", TagName(session, layer.Behavior), layer.TimeSeconds);
        if (layer.Flow.Phase != AnimFlowPhase::None)
            text += std::format(", section {} from {}, loop {}", layer.Flow.Section, layer.Flow.SectionStartTick,
                                layer.Flow.LoopCount);
        machine.Children.push_back(NoteProperty(std::format("{}.layers.{}", key, l), std::format("Layer {}", l), text));
    }
    for (std::size_t r = 0; r < record.Requests.size(); ++r)
        machine.Children.push_back(NoteProperty(std::format("{}.requests.{}", key, r), "Request",
                                                RequestText(session, record.Requests[r])));
    for (std::size_t d = 0; d < record.Decisions.size(); ++d)
        machine.Children.push_back(NoteProperty(std::format("{}.decisions.{}", key, d), "Decision",
                                                std::format("{} {}", AnimDecisionCauseName(record.Decisions[d].Cause),
                                                            AnimChangeReasonName(record.Decisions[d].Reason))));
    return machine;
}

// "guesses.2.tick" as the guess and what of it.
std::optional<std::pair<std::size_t, std::string_view>> GuessOf(std::string_view key)
{
    if (!key.starts_with(kGuesses))
        return std::nullopt;
    key.remove_prefix(kGuesses.size());
    std::size_t index = 0;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), index);
    if (parsed.ec != std::errc{})
        return std::nullopt;
    std::string_view rest = key.substr(static_cast<std::size_t>(parsed.ptr - key.data()));
    if (rest.starts_with('.'))
        rest.remove_prefix(1);
    return std::pair{ index, rest };
}
} // namespace

std::vector<PropertyNode> AnimationLabProperties::Nodes() const
{
    const AnimationLabRun& lab = Workspace.Lab;
    const bool ran = lab.Session != nullptr && lab.Session->IsOpen();
    std::vector<PropertyNode> nodes{ NoteProperty(
        "status", "Lab",
        ran ? lab.Session->Status()
            : std::string("Not run. The lab plays the working scenario on an authority, and on a client that receives "
                          "the authority's requests over a link. Facts are synthetic scenario inputs on both.")) };
    if (!Workspace.Rig.Simulation.IsOpen())
    {
        nodes.push_back(NoteProperty("none", "Rig", "Open a rig to run it on two machines."));
        return nodes;
    }
    PropertyNode link = GroupProperty("link", "Link");
    link.Children = { NumberProperty("link.join", "Join on tick", static_cast<double>(lab.Settings.JoinTick), 0.0, -1.0, 1.0),
                      NumberProperty("link.latency", "Latency, ticks", lab.Settings.LatencyTicks, 0.0, -1.0, 1.0),
                      NumberProperty("link.loss", "Loss, %", lab.Settings.LossPercent, 0.0, 90.0, 1.0),
                      NumberProperty("link.run_to", "Run to tick", static_cast<double>(lab.Tick), 1.0, -1.0, 1.0) };
    link.Descriptor.Verbs = { { .Id = "run", .Label = ran ? "Run Again" : "Run" } };
    if (ran && !(lab.Session->Settings() == lab.Settings))
        link.Children.push_back(NoteProperty("link.changed", "Settings", "Changed since the last run; run again."));
    if (!Problem.empty())
        link.Children.push_back(NoteProperty("link.problem", "Problem", Problem));
    nodes.push_back(std::move(link));

    PropertyNode guesses = GroupProperty("guesses", "Client guesses");
    guesses.Descriptor.Tooltip = "A request the client predicts, and what the authority does with the command behind it.";
    guesses.Descriptor.Verbs = { { .Id = "add", .Label = "Add a Guess" } };
    const std::vector<std::string>& participants = Workspace.Rig.Simulation.Scenario().Participants;
    const std::vector<std::string_view> names(participants.begin(), participants.end());
    for (std::size_t g = 0; g < lab.Injections.size(); ++g)
    {
        const AnimationLabInjection& guess = lab.Injections[g];
        const std::string key = std::format("{}{}", kGuesses, g);
        PropertyNode group = GroupProperty(key, std::format("Guess {}", g + 1));
        group.Descriptor.Verbs = { { .Id = "remove", .Label = "Remove" } };
        group.Children.push_back(NumberProperty(key + ".tick", "Guessed on", static_cast<double>(guess.Tick), 0.0, -1.0, 1.0));
        if (!names.empty())
        {
            const auto by = std::ranges::find(names, guess.Participant);
            group.Children.push_back(ChoiceProperty(key + ".by", "By", names,
                                                    by != names.end() ? static_cast<std::size_t>(by - names.begin()) : 0));
        }
        group.Children.push_back(TextProperty(key + ".intent", "Intent", guess.Intent));
        group.Children.push_back(
            NumberProperty(key + ".decided", "Decided on", static_cast<double>(guess.AuthorityTick), 0.0, -1.0, 1.0));
        group.Children.push_back(ToggleProperty(key + ".confirmed", "Confirmed", guess.Confirmed));
        guesses.Children.push_back(std::move(group));
    }
    nodes.push_back(std::move(guesses));
    if (ran)
    {
        const std::vector<PropertyNode> run = RunNodes();
        nodes.insert(nodes.end(), run.begin(), run.end());
    }
    return nodes;
}

std::vector<PropertyNode> AnimationLabProperties::RunNodes() const
{
    const AnimationSessionLab& lab = *Workspace.Lab.Session;
    std::uint32_t lost = 0;
    std::uint32_t delivered = 0;
    for (const AnimationLabTick& tick : lab.Ticks())
    {
        lost += tick.Lost;
        delivered += tick.Delivered;
    }
    PropertyNode summary = GroupProperty("summary", "Result");
    summary.Children.push_back(
        NoteProperty("summary.snapshots", "Snapshots", std::format("delivered {}, lost {}", delivered, lost)));
    summary.Children.push_back(NoteProperty("summary.agree", "Agreement",
                                            lab.ConvergedAt() ? std::format("from tick {}", *lab.ConvergedAt())
                                                              : std::string("not agreeing at the last tick")));
    if (lab.PendingPredictions() > 0)
        summary.Children.push_back(NoteProperty("summary.pending", "Guesses",
                                                std::format("{} waiting on the authority", lab.PendingPredictions())));
    if (!lab.Ticks().empty() && lab.Ticks().back().TimingDisagrees)
        summary.Children.push_back(NoteProperty("summary.timing", "Timing",
                                                "The client binds this rig with different timing from the authority's: "
                                                "reconstruction cannot be trusted until both load the same content."));
    std::vector<PropertyNode> nodes{ std::move(summary) };

    PropertyNode joined = GroupProperty("joined", "What the joiner received");
    if (lab.JoinedAt())
    {
        joined.Children.push_back(NoteProperty("joined.at", "First snapshot", std::format("tick {}", *lab.JoinedAt())));
        for (std::size_t r = 0; r < lab.JoinRequests().size(); ++r)
            joined.Children.push_back(NoteProperty(std::format("joined.requests.{}", r), "Request",
                                                   RequestText(lab.Client(), lab.JoinRequests()[r])));
    }
    else
        joined.Children.push_back(NoteProperty("joined.none", "Snapshots", "Nothing has arrived yet."));
    nodes.push_back(std::move(joined));

    const AnimTick inspected = std::min(Inspected, lab.Tick());
    nodes.push_back(NumberProperty("inspect", "Tick", static_cast<double>(inspected), 0.0,
                                   static_cast<double>(lab.Tick()), 1.0));
    const AnimationPreviewTickRecord* here = RecordAt(lab.Authority(), inspected);
    const AnimationPreviewTickRecord* there = RecordAt(lab.Client(), inspected);
    if (here == nullptr || there == nullptr)
        nodes.push_back(NoteProperty("machines.none", "Machines", "That tick is no longer kept."));
    else
    {
        nodes.push_back(Machine("authority", "Authority", lab.Authority(), *here));
        nodes.push_back(Machine("client", "Client", lab.Client(), *there));
    }
    return nodes;
}

bool AnimationLabProperties::BeginEdit(std::string_view key)
{
    return key.starts_with("link.") || key.starts_with(kGuesses) || key == "inspect";
}

PropertyEditResult AnimationLabProperties::CommitEdit(std::string_view key, std::string_view value)
{
    AnimationLabRun& lab = Workspace.Lab;
    if (const std::optional<std::pair<std::size_t, std::string_view>> guess = GuessOf(key);
        guess && guess->first < lab.Injections.size())
    {
        AnimationLabInjection& edited = lab.Injections[guess->first];
        if (guess->second == "intent")
            edited.Intent = value;
        else if (guess->second == "by")
            edited.Participant = value;
        else if (guess->second == "confirmed")
        {
            const std::optional<bool> on = ParseToggle(value);
            if (!on)
                return PropertyEditResult::Refused;
            edited.Confirmed = *on;
        }
        else
        {
            const std::optional<double> tick = ParseNumber(value, 0.0, -1.0);
            if (!tick)
                return PropertyEditResult::Refused;
            (guess->second == "tick" ? edited.Tick : edited.AuthorityTick) = static_cast<AnimTick>(std::llround(*tick));
        }
        return PropertyEditResult::Applied;
    }
    const std::optional<double> number = ParseNumber(value, 0.0, key == "link.loss" ? 90.0 : -1.0);
    if (!number)
        return PropertyEditResult::Refused;
    const auto whole = static_cast<std::uint64_t>(std::llround(*number));
    if (key == "link.join")
        lab.Settings.JoinTick = whole;
    else if (key == "link.latency")
        lab.Settings.LatencyTicks = static_cast<std::uint32_t>(whole);
    else if (key == "link.loss")
        lab.Settings.LossPercent = static_cast<std::uint32_t>(whole);
    else if (key == "link.run_to")
        lab.Tick = std::max<std::uint64_t>(whole, 1);
    else if (key == "inspect")
        InspectTick(whole);
    else
        return PropertyEditResult::Gone;
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationLabProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    AnimationLabRun& lab = Workspace.Lab;
    if (key == "link" && verb == "run")
    {
        const bool ran = lab.Run(Workspace.Rig.Simulation);
        Problem = ran ? std::string() : std::string("The rig did not open on both machines.");
        return ran ? PropertyEditResult::Applied : PropertyEditResult::Refused;
    }
    if (key == "guesses" && verb == "add")
    {
        AnimationLabInjection guess;
        guess.Tick = Workspace.Rig.Simulation.Tick();
        guess.AuthorityTick = guess.Tick + 2;
        if (!Workspace.Rig.Simulation.Scenario().Participants.empty())
            guess.Participant = Workspace.Rig.Simulation.Scenario().Participants.front();
        lab.Injections.push_back(std::move(guess));
        return PropertyEditResult::Applied;
    }
    if (const std::optional<std::pair<std::size_t, std::string_view>> guess = GuessOf(key);
        guess && guess->second.empty() && verb == "remove" && guess->first < lab.Injections.size())
    {
        lab.Injections.erase(lab.Injections.begin() + static_cast<std::ptrdiff_t>(guess->first));
        return PropertyEditResult::Applied;
    }
    return PropertyEditResult::Gone;
}

std::optional<WorkspaceTimelineFacet> AnimationLabProperties::Timeline() const
{
    const AnimationLabRun& run = Workspace.Lab;
    if (run.Session == nullptr || !run.Session->IsOpen() || run.Session->Ticks().empty())
        return std::nullopt;
    const AnimationSessionLab& lab = *run.Session;
    const double seconds = lab.Authority().TickSeconds();
    const AnimTick inspected = std::min(Inspected, lab.Tick());
    WorkspaceTimelineFacet facet{ .Clock = "Session lab",
                                  .Start = static_cast<double>(lab.Ticks().front().Tick) * seconds,
                                  .End = static_cast<double>(lab.Tick()) * seconds,
                                  .Time = static_cast<double>(inspected) * seconds,
                                  .Playing = false,
                                  .Live = false,
                                  .Readout = std::format("lab tick {} / {}", inspected, lab.Tick()) };
    facet.Lanes = AnimationLabLanes(lab, inspected);
    return facet;
}

void AnimationLabProperties::InspectAt(double seconds)
{
    const AnimationLabRun& run = Workspace.Lab;
    if (run.Session == nullptr || !run.Session->IsOpen())
        return;
    const double tick = seconds / run.Session->Authority().TickSeconds();
    InspectTick(static_cast<AnimTick>(std::max(0.0, std::round(tick))));
}

void AnimationLabProperties::InspectTick(AnimTick tick)
{
    Inspected = tick;
}
