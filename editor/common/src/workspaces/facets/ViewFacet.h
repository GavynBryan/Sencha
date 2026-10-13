#pragma once

#include <string>

// How the workspace is being seen: which view is in front and how many share
// the content area. The choices are actions in the View category.
struct WorkspaceViewFacet
{
    // Empty when the workspace has no views to arrange.
    std::string Projection = {};
    int Views = 0;
};
