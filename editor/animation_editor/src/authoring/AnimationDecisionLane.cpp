#include "authoring/AnimationDecisionLane.h"

#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimRigBinding.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <charconv>
#include <format>

namespace
{
constexpr std::string_view kRulePrefix = "rule:";

std::string SpanLabel(const AnimationPreviewSession& session, const AnimBoundRig& rig, std::size_t layer,
                      const AnimationPreviewLayerRecord& record)
{
    const int selector = rig.Layers[layer].Selector;
    if (selector >= 0 && record.Winner != kAnimNoRule)
        return rig.Selectors[static_cast<std::size_t>(selector)].Rules[record.Winner].Label;
    const GameplayTagRegistry* tags = session.Tags();
    if (record.Behavior.IsValid() && tags != nullptr)
        return std::string(tags->GetName(record.Behavior));
    return "(none)";
}
} // namespace

TimelineLane AnimationDecisionLane(const AnimationPreviewSession& session, std::size_t layer, int picked)
{
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || layer >= rig->Layers.size())
        return {};
    TimelineLane lane{ .Label = rig->Layers[layer].NameText };
    const double seconds = session.TickSeconds();
    const auto close = [&](double end) {
        if (!lane.Spans.empty())
            lane.Spans.back().End = std::max(end, lane.Spans.back().Start + seconds);
    };
    int held = -2;
    GameplayTagId heldBehavior;
    for (const AnimationPreviewTickRecord& record : session.History())
    {
        if (layer >= record.Layers.size())
            continue;
        const AnimationPreviewLayerRecord& at = record.Layers[layer];
        const int rule = at.Winner == kAnimNoRule ? -1 : static_cast<int>(at.Winner);
        if (rule == held && (rule >= 0 || at.Behavior == heldBehavior))
            continue;
        const double start = static_cast<double>(record.Tick) * seconds;
        close(start);
        lane.Spans.push_back({ .Key = std::format("{}{}:{}", kRulePrefix, layer, rule),
                               .Start = start,
                               .End = start,
                               .Label = SpanLabel(session, *rig, layer, at),
                               .Selected = rule >= 0 && rule == picked });
        held = rule;
        heldBehavior = at.Behavior;
    }
    close(static_cast<double>(session.Tick()) * seconds);
    return lane;
}

std::optional<AnimationRulePick> ParseDecisionSpanKey(std::string_view key)
{
    if (!key.starts_with(kRulePrefix))
        return std::nullopt;
    key.remove_prefix(kRulePrefix.size());
    const std::size_t colon = key.find(':');
    AnimationRulePick pick;
    if (colon == std::string_view::npos
        || std::from_chars(key.data(), key.data() + colon, pick.Layer).ec != std::errc{}
        || std::from_chars(key.data() + colon + 1, key.data() + key.size(), pick.Rule).ec != std::errc{})
        return std::nullopt;
    return pick;
}
