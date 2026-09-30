#pragma once

#include "workspaces/WorkspaceFacets.h"

class EditorWorkspace;
class ElementVerbs;
class FaceUvSource;

// The Level's selection as the shell presents it: named for what it is, with
// the verbs for the element kind in hand and the settings they read.
[[nodiscard]] WorkspaceSelectionFacet SelectionOf(EditorWorkspace& workspace, ElementVerbs& verbs, FaceUvSource& uv);
