#pragma once

#include <gameplay_tags/GameplayTagId.h>

#include <cstddef>
#include <optional>

// Cross-panel selection. Changing it never touches the simulation or scenario.
struct AnimationNavigation
{
    std::size_t Layer = 0;
    int Rule = -1;
    GameplayTagId Behavior;
    int Row = -1;
    int Content = -1;
    int Joint = -1;
    // Index into the simulation's history; empty for the live tick.
    std::optional<std::size_t> InspectRecord;
};

