#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Plane.h>

#include <cstdint>

//=============================================================================
// Brush edit verbs — each a pure function BrushMesh -> BrushMesh, wrapped by an
// undoable command in the editor. Pure so they are unit-tested with zero UI.
// Results are run through BrushValidateAndRepair, so callers get welded, outward-
// oriented geometry. (docs/plans/sencha-level-editor/03-brush-representation.md §3)
//=============================================================================
struct BrushOps
{
    // The default brush: 8 vertices, 6 quad faces, outward normals.
    [[nodiscard]] static BrushMesh MakeBox(Vec3d halfExtents);

    // Whole-brush move (mesh is local space; normally the entity transform moves
    // instead — provided for completeness and testing).
    [[nodiscard]] static BrushMesh Translate(const BrushMesh& mesh, Vec3d delta);

    // Move a face's loop along its normal to a new plane position, clamped so the
    // solid keeps at least minThickness against the opposing geometry.
    [[nodiscard]] static BrushMesh ResizeFace(const BrushMesh& mesh, std::uint32_t face,
                                              float planePosition, float minThickness);

    // Duplicate a face's loop offset along its normal; connect with side quads.
    [[nodiscard]] static BrushMesh ExtrudeFace(const BrushMesh& mesh, std::uint32_t face,
                                               float distance);

    // Remove a face, opening the solid (allowed during authoring).
    [[nodiscard]] static BrushMesh DeleteFace(const BrushMesh& mesh, std::uint32_t face);

    // Insert a vertex at the midpoint of the undirected edge (a, b) into every
    // face loop that traverses it, keeping the mesh 2-manifold. Pure topology:
    // appends one vertex and leaves validation to the caller, so several splits
    // compose by stable vertex indices before a single repair. No-op if the edge
    // is absent.
    [[nodiscard]] static BrushMesh SplitEdge(const BrushMesh& mesh,
                                             std::uint32_t a, std::uint32_t b);

    // Slice by a plane, keep one side, cap the new opening. keepPositiveSide keeps
    // the half-space the plane normal points into. (The Hammer clip tool.)
    [[nodiscard]] static BrushMesh Clip(const BrushMesh& mesh, const Plane& plane,
                                        bool keepPositiveSide);
};
