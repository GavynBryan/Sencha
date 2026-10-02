#include "authoring/AnimationTimelineLanes.h"

#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimFacts.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRigBinding.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <map>

namespace
{
constexpr std::array<std::pair<std::string_view, AnimationLanePick::What>, 4> kPrefixes{ {
    { "rule:", AnimationLanePick::What::Rule },
    { "section:", AnimationLanePick::What::Section },
    { "blend:", AnimationLanePick::What::Blend },
    { "request:", AnimationLanePick::What::Request },
} };

std::string TagName(const AnimationPreviewSession& session, GameplayTagId tag)
{
    const GameplayTagRegistry* tags = session.Tags();
    return tag.IsValid() && tags != nullptr ? std::string(tags->GetName(tag)) : std::string("(none)");
}

// What held on one recorded tick: the same Identity on consecutive ticks is
// one span.
struct Held
{
    std::string Identity;
    std::string Key;
    std::string Label;
    bool Selected = false;
};

template <typename Read>
TimelineLane Runs(const AnimationPreviewSession& session, std::string label, Read read)
{
    TimelineLane lane{ .Label = std::move(label) };
    const double seconds = session.TickSeconds();
    std::optional<std::string> held;
    const auto close = [&](double end) {
        if (held && !lane.Spans.empty())
            lane.Spans.back().End = std::max(end, lane.Spans.back().Start + seconds);
    };
    for (const AnimationPreviewTickRecord& record : session.History())
    {
        const double start = static_cast<double>(record.Tick) * seconds;
        std::optional<Held> now = read(record);
        if (now && held && *held == now->Identity)
            continue;
        close(start);
        held.reset();
        if (!now)
            continue;
        lane.Spans.push_back({ .Key = std::move(now->Key),
                               .Start = start,
                               .End = start,
                               .Label = std::move(now->Label),
                               .Selected = now->Selected });
        held = std::move(now->Identity);
    }
    close(static_cast<double>(session.Tick()) * seconds);
    return lane;
}

std::string LaneKey(AnimationLanePick::What what, std::size_t layer, int index)
{
    const auto prefix = std::ranges::find(kPrefixes, what, &std::pair<std::string_view, AnimationLanePick::What>::second);
    return std::format("{}{}:{}", prefix->first, layer, index);
}
} // namespace

TimelineLane AnimationDecisionLane(const AnimationPreviewSession& session, std::size_t layer, int picked)
{
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || layer >= rig->Layers.size())
        return {};
    const int selector = rig->Layers[layer].Selector;
    const GameplayTagRegistry* tags = session.Tags();
    return Runs(session, rig->Layers[layer].NameText, [&](const AnimationPreviewTickRecord& record) {
        if (layer >= record.Layers.size())
            return std::optional<Held>{};
        const AnimationPreviewLayerRecord& at = record.Layers[layer];
        const int rule = at.Winner == kAnimNoRule ? -1 : static_cast<int>(at.Winner);
        std::string label = selector >= 0 && rule >= 0 ? rig->Selectors[static_cast<std::size_t>(selector)].Rules[at.Winner].Label
                            : at.Behavior.IsValid() && tags != nullptr ? std::string(tags->GetName(at.Behavior))
                                                                         : std::string("(none)");
        return std::optional<Held>{ { .Identity = std::format("{}/{}", rule, at.Behavior.Value),
                                      .Key = LaneKey(AnimationLanePick::What::Rule, layer, rule),
                                      .Label = std::move(label),
                                      .Selected = rule >= 0 && rule == picked } };
    });
}

TimelineLane AnimationFlowLane(const AnimationPreviewSession& session, std::size_t layer)
{
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || layer >= rig->Layers.size())
        return {};
    return Runs(session, rig->Layers[layer].NameText + " flow", [&](const AnimationPreviewTickRecord& record) {
        if (layer >= record.Layers.size())
            return std::optional<Held>{};
        const AnimationPreviewLayerRecord& at = record.Layers[layer];
        const int flow = at.Content < rig->Contents.size() ? rig->Contents[at.Content].Flow : -1;
        if (flow < 0 || at.Flow.Phase == AnimFlowPhase::None)
            return std::optional<Held>{};
        const std::vector<AnimBoundFlowSection>& sections = rig->Flows[static_cast<std::size_t>(flow)].Sections;
        const std::size_t section = at.Flow.Section;
        std::string label = section < sections.size() ? sections[section].TagName : std::string("?");
        if (at.Flow.LoopCount > 0)
            label += std::format(" \xC3\x97{}", at.Flow.LoopCount + 1);
        if (at.Flow.Phase == AnimFlowPhase::Complete)
            label += ", held";
        return std::optional<Held>{
            { .Identity = std::format("{}/{}/{}/{}", flow, section, at.Flow.LoopCount, static_cast<int>(at.Flow.Phase)),
              .Key = LaneKey(AnimationLanePick::What::Section, layer, static_cast<int>(section)),
              .Label = std::move(label) } };
    });
}

TimelineLane AnimationBlendLane(const AnimationPreviewSession& session, std::size_t layer)
{
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || layer >= rig->Layers.size())
        return {};
    TimelineLane lane{ .Label = rig->Layers[layer].NameText + " blends" };
    const double seconds = session.TickSeconds();
    for (const AnimationPreviewTickRecord& record : session.History())
        for (const AnimDecisionRecord& decision : record.Decisions)
            if (decision.Cause == AnimDecisionCause::BlendApplied && decision.Layer == layer)
            {
                const double start = static_cast<double>(decision.Tick) * seconds;
                lane.Spans.push_back(
                    { .Key = LaneKey(AnimationLanePick::What::Blend, layer, static_cast<int>(decision.Tick)),
                      .Start = start,
                      .End = start + std::max(static_cast<double>(decision.BlendSeconds), seconds),
                      .Label = std::format("{} \xE2\x86\x92 {}", TagName(session, decision.PreviousBehavior),
                                           TagName(session, decision.Behavior)) });
            }
    return lane;
}

std::vector<TimelineLane> AnimationTravelLanes(const AnimationPreviewSession& session)
{
    if (!session.Scenario().Movement)
        return {};
    TimelineLane carried{ .Label = "Carried" };
    TimelineLane achieved{ .Label = "Achieved" };
    const double seconds = session.TickSeconds();
    for (const AnimationPreviewTickRecord& record : session.History())
        if (record.Movement && record.Movement->Carried)
        {
            const double time = static_cast<double>(record.Tick) * seconds;
            carried.Samples.push_back({ .Time = time, .Value = record.Movement->Requested.Magnitude() });
            achieved.Samples.push_back({ .Time = time, .Value = record.Movement->Achieved.Magnitude() });
        }
    return { std::move(carried), std::move(achieved) };
}

TimelineLane AnimationFactLane(const AnimationPreviewSession& session, std::string_view fact)
{
    const AnimBoundRig* rig = session.Rig();
    const int slot = rig != nullptr ? rig->FindSlot(std::string(fact)) : -1;
    if (slot < 0)
        return {};
    const AnimFactKind kind = rig->Slots[static_cast<std::size_t>(slot)].Kind;
    TimelineLane lane{ .Label = std::string(fact) };
    const double seconds = session.TickSeconds();
    for (const AnimationPreviewTickRecord& record : session.History())
        if (static_cast<std::size_t>(slot) < record.Facts.size())
            lane.Samples.push_back({ .Time = static_cast<double>(record.Tick) * seconds,
                                     .Value = AnimFactToNumber(kind, record.Facts[static_cast<std::size_t>(slot)]) });
    return lane;
}

TimelineLane AnimationRequestLane(const AnimationPreviewSession& session)
{
    if (session.Rig() == nullptr)
        return {};
    const AnimBoundRig& rig = *session.Rig();
    TimelineLane lane{ .Label = "Requests" };
    const double seconds = session.TickSeconds();
    std::map<std::uint32_t, std::size_t> spans;
    for (const AnimationPreviewTickRecord& record : session.History())
        for (const AnimRequest& request : record.Requests)
        {
            if (!request.Occupied || !IsAnimRequestLive(request, record.Tick))
                continue;
            const double through = static_cast<double>(record.Tick + 1) * seconds;
            if (const auto found = spans.find(request.Id.Sequence); found != spans.end())
            {
                lane.Spans[found->second].End = through;
                continue;
            }
            const AnimBoundIntent* intent = rig.FindIntent(request.Intent);
            spans.emplace(request.Id.Sequence, lane.Spans.size());
            lane.Spans.push_back(
                { .Key = LaneKey(AnimationLanePick::What::Request, 0, static_cast<int>(request.Id.Sequence)),
                  .Start = static_cast<double>(request.StartTick) * seconds,
                  .End = through,
                  .Label = intent != nullptr ? intent->Name : TagName(session, request.Intent) });
        }
    return lane;
}

std::optional<AnimationLanePick> ParseAnimationLaneKey(std::string_view key)
{
    for (const auto& [prefix, what] : kPrefixes)
    {
        if (!key.starts_with(prefix))
            continue;
        const std::string_view rest = key.substr(prefix.size());
        const std::size_t colon = rest.find(':');
        AnimationLanePick pick{ .Names = what };
        if (colon == std::string_view::npos
            || std::from_chars(rest.data(), rest.data() + colon, pick.Layer).ec != std::errc{}
            || std::from_chars(rest.data() + colon + 1, rest.data() + rest.size(), pick.Index).ec != std::errc{})
            return std::nullopt;
        return pick;
    }
    return std::nullopt;
}
