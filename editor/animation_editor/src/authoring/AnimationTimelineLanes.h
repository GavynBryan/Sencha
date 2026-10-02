#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

class AnimationPreviewSession;

// What a lane's span names when picked: a layer, and the rule that won it or
// the flow section it played; -1 for neither.
struct AnimationLanePick
{
    enum class What : std::uint8_t
    {
        Rule,
        Section,
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
[[nodiscard]] std::optional<AnimationLanePick> ParseAnimationLaneKey(std::string_view key);
