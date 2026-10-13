#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <string_view>

class EditorWorkspace;

// Where the work is, outermost first. In a world: the world, then the graph
// and zone the active view's camera is in -- or, outside every zone, the zone
// being edited -- each a part that reveals itself in the hierarchy. In a lone
// level: `documentLabel`. Then what is selected.
[[nodiscard]] WorkspaceLocationFacet LevelBreadcrumb(const EditorWorkspace& workspace, std::string_view documentLabel);
