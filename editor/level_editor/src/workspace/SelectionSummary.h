#pragma once

#include "workspaces/WorkspaceFacets.h"

class EditorWorkspace;
class ElementVerbs;
class EntityVerbs;
class FaceUvSource;
class IPropertySource;

// The Level's selection as the shell presents it: named for what it is, with
// the verbs for the element kind in hand and the settings they read, and a
// single object's components as its details.
[[nodiscard]] WorkspaceSelectionFacet SelectionOf(EditorWorkspace& workspace, ElementVerbs& verbs,
                                                  const EntityVerbs& entityVerbs, FaceUvSource& uv,
                                                  IPropertySource& components);
