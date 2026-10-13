#pragma once

#include "workspaces/facets/TimelineFacet.h"

#include <string_view>

// A workspace's work running over time, as a deck shows and works it.
class IWorkspaceTimeline
{
public:
    virtual ~IWorkspaceTimeline() = default;

    // Not const: a clock reads what it is showing, which may settle it.
    [[nodiscard]] virtual WorkspaceTimelineFacet Facet() = 0;
    // The ruler dragged to `time`, in the clock's seconds.
    virtual void Scrub(double time, TimelineDrag drag) = 0;
    // A movable mark dragged to `time`: one edit from Begin to End.
    virtual void MoveMark(std::string_view key, double time, TimelineDrag drag) = 0;
    // A mark or a lane's span picked, by its key.
    virtual void Pick(std::string_view key) = 0;
};
