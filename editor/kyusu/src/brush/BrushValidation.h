#pragma once

#include "BrushMesh.h"

#include <string>
#include <vector>

//=============================================================================
// Brush mesh validation & repair. Run after every edit and on load: a brush
// that cannot be repaired into usable geometry is rejected (the command does not
// commit), so no half-broken geometry enters the document.
// (docs/plans/sencha-level-editor/03-brush-representation.md §3.1)
//=============================================================================

struct BrushRepairResult
{
    bool Ok = false;       // mesh is usable (may be open during authoring)
    bool Closed = false;   // every edge is shared by exactly two faces
    bool Changed = false;  // repair modified the mesh
    std::vector<std::string> Warnings;
};

// Welds coincident vertices within tolerance and rewrites loops to the merged
// indices. Exposed because edit ops (e.g. clip) create coincident vertices that
// must merge for adjacency to be well-defined.
void BrushWeldVertices(BrushMesh& mesh, float tolerance = 1e-4f);

// Weld, drop unreferenced vertices, remove degenerate faces, recompute normals
// from the current winding, and report closedness. Does NOT re-wind faces, so an
// in-place edit keeps its existing orientation. Idempotent: a repaired mesh
// repairs to itself with Changed == false.
BrushRepairResult BrushValidateAndRepair(BrushMesh& mesh, float weldTolerance = 1e-4f);

// Reorient every face to point outward with a consistent winding, for any
// orientable manifold brush (concave included): flood-fills winding consistency
// across shared edges, then picks the outward global sign by signed volume (closed
// mesh) or a centroid majority vote (open mesh). Recomputes cached normals.
// Call at construction and for an explicit "recalculate normals"; in-place edit
// ops deliberately do not, so they leave authored normals alone.
void BrushOrientFacesOutward(BrushMesh& mesh);
