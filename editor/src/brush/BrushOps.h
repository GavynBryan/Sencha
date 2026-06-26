#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Plane.h>

#include <array>
#include <cstdint>
#include <vector>

// Creation sub-modes for the Brush tool. A primitive is just a BrushMesh built
// from the create-drag box; everything downstream is shape-agnostic.
enum class BrushPrimitive : std::uint8_t { Box, Plane, Cylinder };

struct BrushPrimitiveParams
{
    Vec3d HalfExtents{ 0.5, 0.5, 0.5 };
    int   DepthAxis = 1;     // plane normal / cylinder axis (0=X, 1=Y, 2=Z)
    int   CylinderSides = 12;
};

//=============================================================================
// Brush edit verbs — each a pure function BrushMesh -> BrushMesh, wrapped by an
// undoable command in the editor. Pure so they are unit-tested with zero UI.
// Results are run through BrushValidateAndRepair (weld + recompute normals).
// Construction ops (MakeBox/MakeCylinder/MakePlane/Clip) also orient outward;
// in-place edits preserve the existing winding (FlipFace / the recalc-normals
// verb are the explicit ways to change it).
// (docs/plans/sencha-level-editor/03-brush-representation.md §3)
//=============================================================================
struct BrushOps
{
    // The default brush: 8 vertices, 6 quad faces, outward normals.
    [[nodiscard]] static BrushMesh MakeBox(Vec3d halfExtents);

    // A single flat quad on the two non-depth axes (zero thickness). Open mesh,
    // like the result of DeleteFace; authoring tolerates it.
    [[nodiscard]] static BrushMesh MakePlane(Vec3d halfExtents, int depthAxis);

    // An N-sided prism about depthAxis (clamped to >= 3 sides). Cross-section
    // fills the drag footprint: ring radii are the two non-depth half-extents.
    [[nodiscard]] static BrushMesh MakeCylinder(Vec3d halfExtents, int depthAxis, int sides);

    // Dispatch over the closed primitive set. One pipeline; the sub-mode is data.
    [[nodiscard]] static BrushMesh MakePrimitive(BrushPrimitive kind,
                                                 const BrushPrimitiveParams& params);

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

    // Reverse one face's winding (and its cached normal). The manual counterpart to
    // the recalc-normals verb: repair no longer re-orients, so the flip persists.
    [[nodiscard]] static BrushMesh FlipFace(const BrushMesh& mesh, std::uint32_t face);

    // The quad strip and perpendicular edge ring reached from a seed edge by the
    // loop-cut flood-fill: StripFaces are the quads the cut crosses (the face loop),
    // RingEdges are the parallel edges it would split (the edge ring), each as a
    // sorted (min, max) vertex pair. Empty if the seed edge is absent or touches no
    // quad. Winding-independent (built from face loops, no half-edge twins).
    struct BrushEdgeRing
    {
        std::vector<std::uint32_t> StripFaces;
        std::vector<std::array<std::uint32_t, 2>> RingEdges;
    };
    [[nodiscard]] static BrushEdgeRing TraceEdgeRing(const BrushMesh& mesh,
                                                     std::uint32_t a, std::uint32_t b);

    // Walk the edge loop containing the seed undirected edge (a, b): the connected run
    // of edges that continues "straight on" through each vertex. At a regular valence-4
    // vertex that is the edge whose faces are disjoint from the current edge's (exact
    // topology). At an irregular vertex (a cap rim or open boundary, where a 3-face fan
    // is topologically ambiguous) it is the geometrically straightest edge, taken only
    // if the turn is under 90 degrees. Returns each edge as a sorted (min, max) vertex
    // pair, including the seed. Terminates at sharp corners, dead ends, or a cycle back
    // to the seed; the seed alone if it continues nowhere; empty if the seed is absent.
    // The perpendicular companion to TraceEdgeRing (loop vs ring).
    [[nodiscard]] static std::vector<std::array<std::uint32_t, 2>> TraceEdgeLoop(
        const BrushMesh& mesh, std::uint32_t a, std::uint32_t b);

    // Walk a topological edge loop from the seed undirected edge (a, b), splitting
    // every edge in the loop at `position` (0..1 from a toward b, propagated
    // consistently around the loop so the cut stays parallel) and subdividing each
    // traversed face into two. position 0.5 is the midpoint (orientation-independent).
    // Produces a clean edge ring parallel to the seed edge on a quad-like mesh. Pure
    // topology: appends vertices and faces, leaves validation to the caller. The loop
    // terminates at: a boundary edge, an odd-sided face, or a cycle back to a visited
    // face. No-op if the seed edge is absent.
    [[nodiscard]] static BrushMesh InsertEdgeLoop(const BrushMesh& mesh,
                                                  std::uint32_t a, std::uint32_t b,
                                                  float position = 0.5f);

    // Single-edge cut: split only the seed edge (a, b) at `position` (0..1 from a)
    // and subdivide its adjacent quads, without propagating around the loop. Leaves
    // a T-vertex on each cut face's opposite edge (the open result is tolerated for
    // authoring, as with DeleteFace). `faceIndex` restricts the cut to one of the
    // edge's faces (the one under the cursor); the default 0xFFFFFFFF cuts every
    // adjacent face. No-op if the seed is absent or touches no quad.
    static constexpr std::uint32_t kAllAdjacentFaces = 0xFFFFFFFFu;
    [[nodiscard]] static BrushMesh InsertEdgeCut(const BrushMesh& mesh,
                                                 std::uint32_t a, std::uint32_t b,
                                                 float position = 0.5f,
                                                 std::uint32_t faceIndex = kAllAdjacentFaces);

    // Slice by a plane, keep one side, cap the new opening. keepPositiveSide keeps
    // the half-space the plane normal points into. (The Hammer clip tool.)
    [[nodiscard]] static BrushMesh Clip(const BrushMesh& mesh, const Plane& plane,
                                        bool keepPositiveSide);
};
