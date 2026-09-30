#pragma once

#include "MeshElementKind.h"
#include "selection/SelectableRef.h"

#include <math/geometry/3d/Transform3d.h>

#include <vector>

struct BrushMesh;

// Expand a seed edge into a loop selection on its brush: in Edge mode the edge
// loop continuing through the seed (BrushOps::TraceEdgeLoop), in Face mode the quad
// strip the seed crosses (BrushOps::TraceEdgeRing). The seed is always an edge ref;
// face-mode callers resolve the edge nearest the cursor on the picked face. The
// registry and entity come from the seed ref. Empty if the seed is not an edge or
// cannot be resolved on the mesh.
[[nodiscard]] std::vector<SelectableRef> GatherLoopSelection(const BrushMesh& mesh,
                                                             const Transform3f& transform,
                                                             const SelectableRef& seedEdge,
                                                             MeshElementKind mode);
