#pragma once

#include "../../meshedit/MeshElementKind.h"
#include "../../selection/ISelectionContext.h"

#include <math/Vec.h>

#include <optional>

class LevelScene;

// Computes the world-space pivot for a selection under the given element mode,
// resolving only the refs that match the mode (mixed kinds are ignored):
//   Object -> entity transform position
//   Vertex -> average of selected vertex positions
//   Edge   -> average of selected edge midpoints
//   Face   -> average of selected face centers
// Returns nullopt when nothing in the selection resolves. Shared by the gizmo
// session (hit-testing/drag) and the renderer (drawing the gizmo) so both agree.
[[nodiscard]] std::optional<Vec3d> ComputeSelectionPivot(const LevelScene& scene,
                                                         const SelectionSnapshot& selection,
                                                         MeshElementKind kind);
