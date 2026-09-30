#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Transform3d.h>

// True world AABB of a brush: every vertex transformed, min/max. Offset-aware
// (unlike a pivot-symmetric box), and shared by the selection box, the bounds
// gizmo, and create-from-selection.
[[nodiscard]] Aabb3d BrushWorldBounds(const BrushMesh& mesh, const Transform3f& transform);
