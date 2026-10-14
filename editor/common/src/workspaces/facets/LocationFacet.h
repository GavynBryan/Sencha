#pragma once

#include <string>
#include <vector>

// One part of where the work is, and the item it reveals in the workspace's
// hierarchy explorer.
struct WorkspaceLocationPart
{
    std::string Label;
    std::string Key;
    // The part naming what is selected, rather than where.
    bool Selection = false;
};

// Where the work is, outermost first: for a world, the world, the graph and
// zone the camera is in, and what is selected. `Hierarchy` is the explorer
// the parts reveal items in, and the one Tab opens.
struct WorkspaceLocationFacet
{
    std::vector<WorkspaceLocationPart> Parts;
    std::string Hierarchy;
    // The action that shows the whole of where the work is from above; empty
    // where there is no such view.
    std::string MapAction = {};
};
