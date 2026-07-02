#pragma once

#include "PivotState.h"
#include "meshedit/MeshElementKind.h"
#include "selection/ISelectionContext.h"

#include <math/Vec.h>

#include <optional>

struct ManipulationSink;

// World-space pivot for a selection under the given element mode, resolving only
// the refs that match the mode (mixed kinds ignored):
//   Object -> entity transform position
//   Vertex -> average of selected vertex positions
//   Edge   -> average of selected edge midpoints
//   Face   -> average of selected face centers
// Reads geometry through the generic ManipulationSink (no EditorScene), so the
// gizmo session and the overlay renderer agree on placement. nullopt when nothing
// in the selection resolves. A set pivot.Override short-circuits the computation
// (the transient pivot wins), so all gizmos act about the moved pivot.
[[nodiscard]] std::optional<Vec3d> ComputeSelectionPivot(const ManipulationSink& sink,
                                                         const SelectionSnapshot& selection,
                                                         MeshElementKind kind,
                                                         const PivotState& pivot);
