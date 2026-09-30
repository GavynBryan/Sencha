#pragma once

#include "selection/SelectableRef.h"

#include <span>

class EditorScene;
class ViewportLayout;
struct GridSettings;

// Resolving the working grid's frame from what is selected, and pushing the
// resolved frame at the orthographic views.
//
// The frame math lives in viewport/GridFrame.h and the settings themselves are
// editor-wide state read by tools, manipulators, viewports, and the toolbar.
// What lives here is only the policy in between: which part of a selection an
// anchor comes from, and how a moved frame reaches the fixed views.
//
// None of these are document edits, so none of them belong on the undo stack.
namespace GridEditing
{

// Origin from the selection: a single selected vertex wins as the exact intent,
// otherwise the center of the selection's world bounds. Leaves the origin alone
// when neither resolves (nothing selected, or no selected entity has bounds).
void SetOriginToSelection(GridSettings& grid, const EditorScene& scene,
                          std::span<const SelectableRef> selection);

// Frame aligned to a selected face: origin at its centroid, U along its longest
// edge, V completing the basis in the face plane. Prefers `primary` when it is a
// face, otherwise the first face in the selection. Leaves the frame alone when
// no face resolves.
void AlignToSelectedFace(GridSettings& grid, const EditorScene& scene,
                         std::span<const SelectableRef> selection, SelectableRef primary);

// Points the fixed orthographic views down the frame's axes, so a moved or
// rotated working grid stays axis-aligned in every ortho view. Cheap and
// idempotent; safe to call once per frame.
void SyncOrthoViews(const GridSettings& grid, ViewportLayout& layout);

} // namespace GridEditing
