#pragma once

#include "workspaces/WorkspaceFacets.h"

class ActionCatalog;
class ActionContext;
class EditorWorkspace;

// The Level's manipulation settings as actions, each saying whether it is in
// effect: the element mode, the gizmo's mode and axes, the pivot and origin,
// snapping, the grid's size and frame, framing the selection, and what the
// views show beyond the scene. Whatever presents actions -- the hatch, the
// palette, a readout's popover -- shows and changes them without knowing what
// they are.
void OfferManipulationActions(ActionCatalog& catalog, ActionContext& context, EditorWorkspace& workspace);

// The same settings as the facts a readout shows.
[[nodiscard]] WorkspaceManipulationFacet ManipulationOf(const EditorWorkspace& workspace);
