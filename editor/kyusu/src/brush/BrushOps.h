#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Plane.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Creation sub-modes for the Brush tool. A primitive is just a BrushMesh built
// from the create-drag box; everything downstream is shape-agnostic.
enum class BrushPrimitive : std::uint8_t { Box, Plane, Cylinder };

struct BrushPrimitiveParams
{
    Vec3d HalfExtents{ 0.5, 0.5, 0.5 };
    int   DepthAxis = 1;     // plane normal / cylinder axis (0=X, 1=Y, 2=Z)
    int   CylinderSides = 12;
    int   PlaneSubdivisions = 1; // quads per side of the plane grid
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

    // A flat quad grid on the two non-depth axes (zero thickness), subdivided
    // into subdivisions x subdivisions quads (clamped to >= 1). Open mesh, like
    // the result of DeleteFace; authoring tolerates it.
    [[nodiscard]] static BrushMesh MakePlane(Vec3d halfExtents, int depthAxis, int subdivisions = 1);

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
    // `inherit` (usually a face adjacent to the seed edge) supplies the strip's
    // material and UV scale/offset/rotation so the texture continues off the
    // edge; null keeps the previous default (level-default material, unit UVs).
    [[nodiscard]] static BrushMesh ExtrudeEdge(const BrushMesh& mesh,
                                               std::uint32_t a, std::uint32_t b, Vec3d offset,
                                               const FaceMaterial* inherit = nullptr);

    // Extrude a set of faces along their own normals as one shell: every
    // selected cap translates `distance` along the blended normal at each of
    // its vertices (a vertex shared by several selected faces moves along
    // their normalized normal sum, so an adjacent ring extrudes together
    // without tearing), and side walls are minted only on the region
    // boundary. Positive distance pushes outward along the normals, negative
    // pulls inward. Wall materials continue the unselected neighbor across
    // each boundary edge, with UV axes re-derived per wall normal (the
    // ExtrudeFaceAlong rule). Invalid or duplicate face indices are ignored;
    // zero distance or an empty set is a no-op. Repairs the result.
    [[nodiscard]] static BrushMesh ExtrudeFacesAlongNormals(const BrushMesh& mesh,
                                                            std::span<const std::uint32_t> faces,
                                                            float distance);

    // Directional region extrude: every selected cap translates by the same
    // `offset` (the gizmo drag vector) as one shell. Shares the region rules
    // of ExtrudeFacesAlongNormals: interior edges between selected faces stay
    // internal topology (no walls); only the region boundary is walled. This
    // is the multi-face form the gizmo extrude must use; composing
    // ExtrudeFaceAlong per face would sweep the shared edges into internal
    // walls. Repairs the result.
    [[nodiscard]] static BrushMesh ExtrudeFacesAlong(const BrushMesh& mesh,
                                                     std::span<const std::uint32_t> faces,
                                                     Vec3d offset);

    // Bevel the selected undirected edges: each edge is replaced by a chamfer
    // strip of `segments` quads spanning `width` world units into each
    // adjacent face, with intermediate rows on a circular blend between the
    // two face planes (segments = 1 is a flat chamfer). Selected edges must
    // be manifold (exactly two faces) and not near-coplanar, and may chain
    // through shared vertices into runs and closed rings (a box rim bevels
    // watertight); a vertex incident to more than two selected edges (a
    // corner fan) is refused. Faces neither side of a beveled edge that touch
    // a chain-end vertex absorb the profile rows into their loop (the cap
    // ngon). Strip materials continue each edge's first face with UV axes
    // re-derived per quad normal. Returns the mesh unchanged on refusal.
    // Repairs the result.
    [[nodiscard]] static BrushMesh BevelEdges(const BrushMesh& mesh,
                                              std::span<const std::array<std::uint32_t, 2>> edges,
                                              float width,
                                              int segments);

    // Remove a face, opening the solid (allowed during authoring).
    [[nodiscard]] static BrushMesh DeleteFace(const BrushMesh& mesh, std::uint32_t face);

    // Reverse one face's winding (and its cached normal). The manual counterpart to
    // the recalc-normals verb: repair no longer re-orients, so the flip persists.
    [[nodiscard]] static BrushMesh FlipFace(const BrushMesh& mesh, std::uint32_t face);

    // Reverses every face (inside-out mesh): the "inner" variant of a pending
    // primitive, for rooms built from a single brush seen from within.
    [[nodiscard]] static BrushMesh FlipAllFaces(const BrushMesh& mesh);

    // Merge the two faces sharing the undirected edge (a, b) into one ngon,
    // removing the edge but keeping its endpoint vertices in the merged loop.
    // The merged face keeps the first adjacent face's material and recomputes
    // its normal. Returns the mesh unchanged when the edge is absent, on a
    // boundary, non-manifold (not exactly two faces), or when the two faces
    // share more than that one edge (the merge would produce a non-simple
    // loop). Leaves validation to the caller.
    [[nodiscard]] static BrushMesh DissolveEdge(const BrushMesh& mesh,
                                                std::uint32_t a, std::uint32_t b);

    // Collapse clusters of the given vertices lying within `distance` of each
    // other (mesh-local units) onto their cluster centroid. Positions only: the
    // now-coincident vertices merge in the caller's validate/repair pass.
    // Out-of-range indices are ignored.
    [[nodiscard]] static BrushMesh WeldVertices(const BrushMesh& mesh,
                                                std::span<const std::uint32_t> vertices,
                                                float distance);

    // Bridge two equal-length vertex paths with a grid of quads: pathA and
    // pathB are ordered runs of existing vertex indices (>= 2 each, same
    // count, disjoint). `closed` closes the final column back to the first.
    // `segments` rows of quads span the gap; with more than
    // one, the intermediate rows follow a Hermite blend seeded by each path's
    // boundary tangent (the summed away-from-face directions of the faces
    // bordering the path's edges), so bridging two perpendicular wall edges
    // bows into a curved hallway shell. Columns whose bordering faces cancel
    // (coplanar interiors) blend straight. Quads wind to continue pathA's
    // bordering face across the shared edge. Pure append; leaves validation to
    // the caller. Returns the mesh unchanged on invalid paths. `inherit`
    // supplies the strip's material as in ExtrudeEdge.
    [[nodiscard]] static BrushMesh BridgeEdgePaths(const BrushMesh& mesh,
                                                   std::span<const std::uint32_t> pathA,
                                                   std::span<const std::uint32_t> pathB,
                                                   int segments,
                                                   const FaceMaterial* inherit = nullptr,
                                                   bool closed = false);

    // One side of a cross-mesh bridge, resolved by the caller into a single
    // space (world in the editor): ordered positions, per-vertex departure
    // tangents (empty, or one unit vector per position with zero meaning
    // straight), whether the run closes into a loop, and for open runs the
    // source face winding (whether a bordering face traverses the run in path
    // order), which drives the strip's facing.
    struct BridgePathSpec
    {
        std::vector<Vec3d> Positions;
        std::vector<Vec3d> Tangents;
        bool Closed = false;
        bool WindsForward = false;
    };

    // A standalone bridge mesh spanning two resolved paths (no source mesh
    // geometry is copied; the caller owns committing it as a new brush).
    // Closed loops orient deterministically from geometry: each loop's Newell
    // normal against the axis between the two centroids decides quad facing
    // and whether b pairs reversed, and least-squares distance picks only the
    // rotation. (Winding flags cannot decide this: on a closed manifold every
    // edge borders two faces traversing it in opposite directions, so a flag
    // read from "the" bordering face is a coin flip; the distance-only pairing
    // it fed also tied on symmetric loops. Both fed the twist.) Open runs pair
    // by distance (their endpoints anchor the choice) and face by a's winding
    // flag. Interior rows follow the same Hermite blend as BridgeEdgePaths.
    // Returns an empty mesh when the paths cannot pair.
    [[nodiscard]] static BrushMesh BuildBridgeBetweenPaths(BridgePathSpec a,
                                                           BridgePathSpec b,
                                                           int segments,
                                                           const FaceMaterial* inherit = nullptr);

    // Per-vertex bridge departure tangents for a path of existing mesh
    // vertices: the summed away-from-face directions of the faces bordering
    // each incident path edge. Coplanar interiors cancel to zero (straight); a
    // box rim yields the corner bisector, so a bridge leaving it bows outward.
    // `closed` wraps the last edge back to the first. Unit or zero vectors,
    // mesh-local space.
    [[nodiscard]] static std::vector<Vec3d> PathBoundaryTangents(const BrushMesh& mesh,
                                                                 std::span<const std::uint32_t> path,
                                                                 bool closed);

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
    // the half-space the plane normal points into. (The classic clip tool.)
    [[nodiscard]] static BrushMesh Clip(const BrushMesh& mesh, const Plane& plane,
                                        bool keepPositiveSide);

    // The orthonormal in-plane frame of a flat rectangular quad face, or nullopt
    // when the face is not one (non-quad, degenerate, sheared, non-planar).
    // Origin is Loop[0]; AxisU runs Loop[0] -> Loop[1]; AxisV runs
    // Loop[0] -> Loop[3]. For a CCW outward loop, AxisU x AxisV is the outward
    // normal (the carve builds its winding on this).
    struct BrushRectFaceFrame
    {
        Vec3d Origin;
        Vec3d AxisU;
        Vec3d AxisV;
        float Width = 0.0f;  // extent along AxisU
        float Height = 0.0f; // extent along AxisV
    };
    [[nodiscard]] static std::optional<BrushRectFaceFrame> RectFaceFrame(const BrushMesh& mesh,
                                                                         std::uint32_t face);

    // Insert loop cuts at the interior bounds of a rectangle authored in a
    // rectangular face frame. Flush bounds are omitted; every non-flush bound
    // must resolve to a full InsertEdgeLoop cut, or already coincide with an
    // existing edge (a rect side flush with an existing loop adds no cut
    // there), or the mesh is returned unchanged. This is the pure topology
    // kernel behind Face Carve's Edge Loop mode.
    [[nodiscard]] static BrushMesh InsertFaceLoopBounds(const BrushMesh& mesh, std::uint32_t face,
                                                        Vec2d rectMin, Vec2d rectMax);

    // Pierce variant of InsertFaceLoopBounds: inserts the full wrapping loop
    // cuts, then removes the loop-bounded rect faces on `face` and the nearest
    // directly opposite rectangular face and bridges them into a tunnel. The
    // loops mint the two openings' corners, so their alignment falls out of
    // the cuts themselves. A rect side flush with the face edge opens a notch
    // instead of walling: the strip the loops cut through that side's
    // bordering face is removed and the wall omitted (requires box-like flat
    // rectangular sides). On an OPEN mesh (a plane) with no opposite face the
    // pierce degrades to cutting the loops and removing the bounded rect face
    // (a hole). Returns the mesh unchanged when a closed solid has no eligible
    // opposite face, the loops do not reach it cleanly (e.g. blocked by
    // non-quad side topology), the rect covers the face, or only an opposite
    // pair of sides is flush (the channel would split the brush in two).
    [[nodiscard]] static BrushMesh InsertFaceLoopBoundsThrough(const BrushMesh& mesh, std::uint32_t face,
                                                               Vec2d rectMin, Vec2d rectMax);

    // Re-topologize a flat rectangular quad face around an inset rectangle given
    // in RectFaceFrame coordinates (rectMin/rectMax are (u,v) pairs, any corner
    // order, unclamped). The rectangle becomes its own face, appended LAST (the
    // caller's handle to it: Faces.back(); success is a grown face count); the
    // surround decomposes into one convex trapezoid ring quad per non-flush side
    // via corner-diagonal edges. A rect side within snap tolerance of a host
    // side clamps flush: that ring quad is omitted and the flush rect corners
    // become split vertices ON the host edge, inserted as SHARED vertices into
    // every other face bordering it (the neighbor gains collinear loop vertices
    // and may become a hexagon: legal). All produced faces are quads carrying
    // the host's FaceMaterial verbatim (coplanar + projective UVs = exact
    // texture continuity). Pure topology like InsertEdgeLoop: validation is the
    // caller's. Returns the mesh unchanged when the face is not a rectangular
    // quad, the rect is empty after clamping, or the rect covers the whole face.
    [[nodiscard]] static BrushMesh CarveFaceRect(const BrushMesh& mesh, std::uint32_t face,
                                                 Vec2d rectMin, Vec2d rectMax);

    // Carve a rectangular opening through `face` and the first opposite
    // near-parallel rectangular face that contains the projection along -face
    // normal. The two center cap faces are removed and their loops are bridged
    // into a closed tunnel. A rect side flush with the face edge (snap
    // tolerance) opens a notch instead of walling: the bordering face on that
    // side is cut so the channel is open (requires box-like flat rectangular
    // sides bordering both caps; source and target must be flush on matching
    // sides). On an OPEN mesh (a plane) with no opposite face the pierce
    // degrades to CarveFaceRect with the center face removed (a hole).
    // Returns the mesh unchanged when a closed solid has no eligible opposite
    // face, the projected rectangle is not aligned to that face's frame, the
    // rect covers the face, or only an opposite pair of sides is flush (the
    // channel would split the brush in two).
    [[nodiscard]] static BrushMesh CarveFaceRectThrough(const BrushMesh& mesh, std::uint32_t face,
                                                        Vec2d rectMin, Vec2d rectMax);

};
