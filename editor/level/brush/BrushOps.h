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

    // Duplicate a face's loop offset by an arbitrary vector; connect with side
    // quads. The general form behind ExtrudeFace (which offsets along the normal).
    // The gizmo extrude-drag passes the axis-constrained drag vector here so the
    // new cap follows whichever arrow was grabbed. Side walls get a UV projection
    // for their own normal, not the cap's (so the texture is not stretched).
    [[nodiscard]] static BrushMesh ExtrudeFaceAlong(const BrushMesh& mesh, std::uint32_t face,
                                                    Vec3d offset);

    // Pull a new quad plane out of the undirected edge (a, b): duplicate the two
    // endpoints offset by `offset` and bridge with one face. Opens the mesh (the
    // edge ends up shared by more than two faces / on a boundary); authoring
    // tolerates that, as with DeleteFace. Pure append (like SplitEdge): leaves
    // validation to the caller so several edge extrudes compose on stable base
    // indices before a single repair. No-op if a == b or either index is absent.
    [[nodiscard]] static BrushMesh ExtrudeEdge(const BrushMesh& mesh,
                                               std::uint32_t a, std::uint32_t b, Vec3d offset);

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
