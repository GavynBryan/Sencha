#pragma once

#include "brush/BrushFaceFrame.h"
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "brush/CarveStatus.h"

#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// How close a carve point has to be to an existing vertex or edge before it is
// snapped onto it. Twice the weld tolerance, so a point that snapped but did
// not land exactly flush still welds rather than leaving a permanent sliver.
inline constexpr float kCarveSnapTolerance = 2e-4f;

struct CarveSuccess
{
    BrushMesh Mesh;
    // The opening's faces, when the carve left any: one for a shape laid into
    // a face, several for one laid across the cells of a loop grid. A carve
    // that went all the way through has no face there at all, which is a fact
    // about the geometry rather than a state to be guessed from an index.
    std::vector<std::uint32_t> CutFaces;
    std::vector<std::uint32_t> SurroundFaces; // what is left of the host around the opening
    std::vector<std::uint32_t> TunnelWalls;   // empty unless the carve went through
};

// The result of a carve: the new mesh, or the reason there is none.
//
// A refusal carries no mesh at all. The alternative -- handing back a copy of
// the input -- lets a caller commit an unchanged mesh believing it carved, and
// leaves it detecting refusal by comparing face counts, which is how the tool
// used to do it.
class CarveOutcome
{
public:
    [[nodiscard]] static CarveOutcome Success(CarveSuccess value);
    [[nodiscard]] static CarveOutcome Failure(CarveStatus status);

    [[nodiscard]] CarveStatus Status() const { return Reason; }
    [[nodiscard]] bool Ok() const { return Carved.has_value(); }
    [[nodiscard]] const CarveSuccess& Value() const { return *Carved; }
    [[nodiscard]] CarveSuccess&& Take() { return std::move(*Carved); }

private:
    CarveOutcome() = default;

    CarveStatus Reason = CarveStatus::Ok;
    std::optional<CarveSuccess> Carved;
};

// Cut `outline` out of `face`, leaving the opening as its own face and the rest
// of the host as however many faces the shape requires.
//
// `outline` is in `frame` coordinates, counter-clockwise, and must be simple
// and inside the face. Points within the snap tolerance of the host rim are
// pulled onto it: onto a rim vertex where one is that close, otherwise onto the
// rim edge, which is split so the neighbouring face gains the same vertex. That
// snap is what lets a carve sit flush against an existing edge, including one
// an earlier carve left, instead of only against the four sides of a quad.
//
// Every face produced inherits the host's material verbatim. The faces are
// coplanar with projective UVs, so the texture is continuous across them.
// Validation is the caller's, as with the other topology operations.
[[nodiscard]] CarveOutcome CarveFacePolygon(const BrushMesh& mesh, std::uint32_t face,
                                            const BrushFaceFrame& frame,
                                            std::span<const Vec2d> outline);

// Cut `outline` through the brush: out of `face`, out of the first face
// opposite it along the face normal, and open the tunnel between them.
//
// The walls are planar by construction, since for an edge (A, B) projected
// along a fixed direction all three difference vectors lie in the span of
// (B - A) and that direction, whatever the angle between the two faces. What
// can bend a wall is snapping the projected outline onto the far face's rim, so
// planarity is checked after the snap and reported as NonPlanarTunnelWall. The
// one guard on the projection itself is that it not be singular: a direction
// parallel to the target plane collapses a dimension.
//
// An open mesh with nothing opposite degrades to punching the shape out of the
// face, which is the hole a plane can have.
[[nodiscard]] CarveOutcome CarveFacePolygonThrough(const BrushMesh& mesh, std::uint32_t face,
                                                   const BrushFaceFrame& frame,
                                                   std::span<const Vec2d> outline, float planarTol);

// A face named by where its corners are, in loop order. Every kernel call
// renumbers the face list, so anything that has to find a face again after one
// keeps this rather than an index.
struct FaceCorners
{
    std::vector<Vec3d> Corners;
};
[[nodiscard]] FaceCorners CornersOf(const BrushMesh& mesh, std::uint32_t face);

// The faces edge-connected to `face` that continue its surface: on its plane,
// facing the same way, reached across shared edges only. Same plane and
// opposite facing is not the same surface, whatever a seam test may accept.
// The result includes `face` and is in flood order.
[[nodiscard]] std::vector<std::uint32_t> CoplanarSurface(const BrushMesh& mesh, std::uint32_t face,
                                                         float planarTol);

// Lay `outline` (in `frame`) over `faces`, which all lie on the frame's plane.
// The outline is clipped to each face and carved piece by piece; a piece that
// fills its face is that face as it stands (flat) or a whole-face tunnel
// (pierce); tunnels through neighbouring pieces merge where they meet, the
// shared wall being a bent neighbour to the next carve and opened as a notch.
//
// Succeeds only when the pieces cover the outline entirely: the faces are a
// planar partition, so the pieces' areas summing to the outline's is the proof,
// and anything short is OutsideHost. Face extents are never taken as evidence.
// An outline lying wholly within one face runs that face's plain kernel on the
// outline itself, so a carve that never crosses an edge is what it always was.
// Which faces the outline reaches is decided by polygon overlap. A piece is
// the outline's intersection with a face, computed by half-plane clipping,
// which needs one of the two to be convex: a convex face clips the outline, a
// concave face is clipped by a convex outline. A concave face whose piece
// would come out in two parts -- the shape spans a hole an earlier carve left
// -- is refused as ChannelCrossesHole, and a concave shape over a concave
// face as HostNotConvex.
[[nodiscard]] CarveOutcome CarveAcrossSurface(const BrushMesh& mesh, std::span<const FaceCorners> faces,
                                              const BrushFaceFrame& frame, std::span<const Vec2d> outline,
                                              bool pierce, float planarTol);

// Quad mode's operation for any shape: the wrapping loop cuts at the box
// bounds, a further loop through every vertex of `outline` on the box cell's
// rim that is not one of its corners, and the outline laid into the cells
// those loops leave, cell by cell. A shape that fills the box cell -- the box
// itself -- lays nothing further, which is what the loop cuts alone have
// always produced; with `pierce` that is the through loop cut, byte for byte.
//
// The rim vertices are where the shape meets the faces around it: the end of a
// jamb, an apex touching the top, a turned rectangle's points. A loop through
// each one wraps the brush like the box loops do, so every face outside the
// carve stays a quad, and the shape becomes as many faces as those loops cut
// it into. With `pierce` each cell is tunnelled and the tunnels merge where
// they meet: the wall between them is a bent neighbour to the next cell's carve
// and is opened as a notch.
//
// The box is given in the face's quad frame, which is what loop cuts are defined
// in and what finds the cells afterwards. The outline is given in the face's
// canonical frame and is carried into each cell's own frame through world space.
// "Fills the cell" is region equivalence, not equal area: every vertex of each
// outline lies on the other, and the areas agree to within a snap-width sliver
// around the rim. The test errs toward laying the shape in, so a shape that
// leaves a thin surround keeps it rather than having it silently absorbed.
[[nodiscard]] CarveOutcome CarveWithinLoopBounds(const BrushMesh& mesh, std::uint32_t face,
                                                 const BrushOps::BrushRectFaceFrame& quad,
                                                 Vec2d rectMin, Vec2d rectMax,
                                                 const BrushFaceFrame& frame,
                                                 std::span<const Vec2d> outline, bool pierce,
                                                 float planarTol);
