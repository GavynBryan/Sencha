#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class AnimationPreviewSession;

// What a lane's span names when picked: a layer, and the rule that won it,
// the flow section it played or the tick a blend began; -1 for none.
struct AnimationLanePick
{
    enum class What : std::uint8_t
    {
        Rule,
        Section,
        Blend,
        Request,
    };
    What Names = What::Rule;
    std::size_t Layer = 0;
    int Index = -1;
};

// What won `layer` along the simulation's recorded ticks: a span per stretch
// one rule held, named for the rule, or for the behavior on a layer chosen by
// requests. The span of rule `picked` is selected.
[[nodiscard]] TimelineLane AnimationDecisionLane(const AnimationPreviewSession& session, std::size_t layer,
                                                 int picked);
// The flow sections `layer` played along the recorded ticks: a span per pass
// through a section, none while it plays no flow.
[[nodiscard]] TimelineLane AnimationFlowLane(const AnimationPreviewSession& session, std::size_t layer);
// The blends applied on `layer` along the recorded ticks, each a span as long
// as it ran.
[[nodiscard]] TimelineLane AnimationBlendLane(const AnimationPreviewSession& session, std::size_t layer);
// How far root motion carried the character on each recorded tick, and how
// far it got: two plotted lanes, none while it poses in place.
[[nodiscard]] std::vector<TimelineLane> AnimationTravelLanes(const AnimationPreviewSession& session);
// Fact `fact` on each recorded tick, plotted; empty when the rig has no such fact.
[[nodiscard]] TimelineLane AnimationFactLane(const AnimationPreviewSession& session, std::string_view fact);
// Each request issued along the recorded ticks, a span from its start for as
// long as it was live; the index a span names is the request's sequence.
[[nodiscard]] TimelineLane AnimationRequestLane(const AnimationPreviewSession& session);
[[nodiscard]] std::optional<AnimationLanePick> ParseAnimationLaneKey(std::string_view key);
