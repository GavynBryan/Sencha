#pragma once

#include "BrushMesh.h"

#include <cstdint>
#include <vector>

//=============================================================================
// Transient half-edge view over a face-vertex BrushMesh, for edit ops that need
// neighbor traversal (weld, clip cap ordering, future carve). Built on demand,
// never persisted. Pure + round-trippable with the face-vertex form.
// (docs/plans/sencha-level-editor/03-brush-representation.md §3)
//=============================================================================

inline constexpr std::uint32_t BrushInvalidIndex = 0xFFFFFFFFu;

struct BrushHalfEdge
{
    std::uint32_t Origin = 0;                 // vertex this half-edge starts at
    std::uint32_t Next   = BrushInvalidIndex; // next half-edge around the same face
    std::uint32_t Twin   = BrushInvalidIndex; // opposite half-edge, or invalid on a boundary
    std::uint32_t Face   = BrushInvalidIndex; // face this half-edge borders
};

struct BrushHalfEdgeMesh
{
    std::vector<BrushVertex>    Vertices;
    std::vector<BrushHalfEdge>  HalfEdges;
    std::vector<std::uint32_t>  FaceToHalfEdge; // one half-edge per face
};

[[nodiscard]] BrushHalfEdgeMesh BrushBuildHalfEdge(const BrushMesh& mesh);

// Inverse: reconstruct the face-vertex mesh (loops + recomputed normals).
[[nodiscard]] BrushMesh BrushToFaceVertex(const BrushHalfEdgeMesh& halfEdge);
