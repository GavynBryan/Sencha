#pragma once

#include "FaceMaterial.h"

#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <array>
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
    // Undirected edges shaded smooth: tessellation averages the vertex normal
    // across the two faces meeting at a soft edge instead of the default hard
    // (per-face) normal. Stored as canonical (min, max) vertex pairs; edges
    // absent from the list are hard. Repair remaps and prunes the pairs, so
    // membership survives welds and vertex compaction.
    std::vector<std::array<std::uint32_t, 2>> SoftEdges;
};

// Canonical (min, max) form for SoftEdges membership.
[[nodiscard]] inline std::array<std::uint32_t, 2> BrushSoftEdgeKey(std::uint32_t a, std::uint32_t b)
{
    return a < b ? std::array<std::uint32_t, 2>{ a, b } : std::array<std::uint32_t, 2>{ b, a };
}

// Whether the undirected edge (a, b) is marked soft.
[[nodiscard]] bool BrushEdgeIsSoft(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b);

// Add or remove the undirected edge (a, b) from the soft set (idempotent).
void BrushSetEdgeSoft(BrushMesh& mesh, std::uint32_t a, std::uint32_t b, bool soft);

// Newell's method — robust polygon normal consistent with CCW winding. Returns a
// normalized vector, or {0,0,0} for a degenerate loop.
[[nodiscard]] Vec3d BrushComputeFaceNormal(const BrushMesh& mesh, const BrushFace& face);

// Average of a face's loop vertex positions.
[[nodiscard]] Vec3d BrushFaceCentroid(const BrushMesh& mesh, const BrushFace& face);

// Average of all vertex positions (mesh "center" for outward-orientation tests).
[[nodiscard]] Vec3d BrushMeshCentroid(const BrushMesh& mesh);

// Axis-aligned bounds over all vertices.
[[nodiscard]] Aabb3d BrushComputeBounds(const BrushMesh& mesh);
