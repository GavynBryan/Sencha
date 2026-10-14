#pragma once

#include "workspaces/facets/SelectionFacet.h"

class EditorWorkspace;
class ElementVerbs;
class EntityVerbs;
class ComponentPropertySource;
class FaceUvSource;
class LightingDiagnostics;
class PartitionOutline;

// The Level's selection as the shell presents it: named for what it is, with
// the verbs for the element kind in hand and the settings they read, and a
// single object's components as its details and its part in the lighting beside
// them, its name the title it is renamed by. With no entity selected, what the
// zones outline picked is the selection.
[[nodiscard]] WorkspaceSelectionFacet SelectionOf(EditorWorkspace& workspace, ElementVerbs& verbs,
                                                  const EntityVerbs& entityVerbs, FaceUvSource& uv,
                                                  ComponentPropertySource& components, PartitionOutline& partition,
                                                  LightingDiagnostics& lighting);
