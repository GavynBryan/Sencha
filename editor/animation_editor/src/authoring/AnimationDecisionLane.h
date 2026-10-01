#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <cstddef>
#include <optional>
#include <string_view>

class AnimationPreviewSession;

// A layer's rule, as a decision lane's span names it; -1 when no rule won.
struct AnimationRulePick
{
    std::size_t Layer = 0;
    int Rule = -1;
};

// What won `layer` along the simulation's recorded ticks: a span per stretch
// one rule held, named for the rule, or for the behavior on a layer chosen by
// requests. The span of `picked` is selected.
[[nodiscard]] TimelineLane AnimationDecisionLane(const AnimationPreviewSession& session, std::size_t layer,
                                                 int picked);
[[nodiscard]] std::optional<AnimationRulePick> ParseDecisionSpanKey(std::string_view key);
