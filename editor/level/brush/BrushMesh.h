#pragma once

#include "FaceMaterial.h"

#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>
#include <vector>

//=============================================================================
// BrushMesh — the authored brush geometry: a closed, orientable polygon mesh
// stored as indexed face-vertex. Faces are polygons (a box is 6 quads, not 12
// tris); triangulation happens only at render/cook time. Non-convex allowed.
// (docs/plans/sencha-level-editor/03-brush-representation.md §2.1)
//=============================================================================

struct BrushVertex
{
    Vec3d Position; // local space; UVs are per-face and live in texturing (04-)
};

struct BrushFace
{
    std::vector<std::uint32_t> Loop;     // CCW vertex indices, outward-facing (>=3)
    Vec3d                      Normal = {}; // cached; recomputed on edit (Newell)
    FaceMaterial               Material;  // per-face texturing; survives edits (04-)
};

struct BrushMesh
{
    std::vector<BrushVertex> Vertices;
    std::vector<BrushFace>   Faces;
};

// Newell's method — robust polygon normal consistent with CCW winding. Returns a
// normalized vector, or {0,0,0} for a degenerate loop.
[[nodiscard]] Vec3d BrushComputeFaceNormal(const BrushMesh& mesh, const BrushFace& face);

// Average of a face's loop vertex positions.
[[nodiscard]] Vec3d BrushFaceCentroid(const BrushMesh& mesh, const BrushFace& face);

// Average of all vertex positions (mesh "center" for outward-orientation tests).
[[nodiscard]] Vec3d BrushMeshCentroid(const BrushMesh& mesh);

// Axis-aligned bounds over all vertices.
[[nodiscard]] Aabb3d BrushComputeBounds(const BrushMesh& mesh);
