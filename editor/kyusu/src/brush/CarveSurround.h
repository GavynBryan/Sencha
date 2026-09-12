#pragma once

#include "brush/CarveStatus.h"

#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// The carve's 2D half: what is left of a face once a shape has been cut out of
// it. Pure geometry in frame coordinates -- no mesh, no ImGui, no tolerance
// beyond the one it is handed -- because this is where the operation is most
// likely to be wrong and least pleasant to debug through a mesh.

enum class PointPolygonRelation
{
    Outside,
    Inside,
    Boundary,
};

// Where `point` sits relative to a simple polygon. It classifies rather than
// answering yes or no because its two callers want different boundary
// semantics: picking counts the boundary as a hit, while the carve has to tell
// a contact run from a crossing.
[[nodiscard]] PointPolygonRelation ClassifyPointInPolygon2D(std::span<const Vec2d> polygon, Vec2d point,
                                                            float tolerance);

// Signed area; positive for a counter-clockwise ring.
[[nodiscard]] float PolygonSignedArea(std::span<const Vec2d> polygon);

// Distance from `point` to the segment [a, b], clamped to its ends.
[[nodiscard]] float DistanceToSegment2D(Vec2d point, Vec2d a, Vec2d b);

// Snap `point` onto `rim` when it is within `tolerance`: onto a rim vertex when
// one is that close, else onto the nearest rim edge, else unchanged.
//
// Vertices are tried before edges because projecting onto a line can leave a
// point a fraction past the weld tolerance from the vertex it was meant to land
// on, and that fraction becomes a sliver edge nothing later removes.
[[nodiscard]] Vec2d SnapPointToPolygon2D(std::span<const Vec2d> rim, Vec2d point, float tolerance);

// Whether `polygon` is simple: at least three points, none repeated within
// `tolerance`, and no two edges meeting anywhere but at a shared endpoint.
[[nodiscard]] bool IsSimplePolygon2D(std::span<const Vec2d> polygon, float tolerance);

// The part of `polygon` inside `convex` (counter-clockwise): the polygon
// clipped against each of the convex polygon's edge half-planes in turn, with
// any points that land within `tolerance` of each other merged. Empty when the
// polygon only touches the region or misses it. A simple polygon clipped by a
// convex region stays simple, which is what lets a shape be laid face by face.
[[nodiscard]] std::vector<Vec2d> ClipPolygonToConvex2D(std::span<const Vec2d> polygon,
                                                       std::span<const Vec2d> convex, float tolerance);

// ClipPolygonToConvex2D against the axis-aligned rectangle [min, max].
[[nodiscard]] std::vector<Vec2d> ClipPolygonToRect2D(std::span<const Vec2d> polygon, Vec2d min, Vec2d max,
                                                     float tolerance);

// Whether a simple counter-clockwise polygon is convex: no right turn at any
// vertex, collinear runs allowed.
[[nodiscard]] bool IsConvexPolygon2D(std::span<const Vec2d> polygon, float tolerance);

// Whether two simple polygons share interior: a vertex of either strictly
// inside the other, two edges properly crossing, or an interior sample of one
// (an edge midpoint, a chord across a convex corner) inside the other, which is
// what tells two coincident polygons from two that merely touch. Touching along
// a boundary or at a point is not overlap. This is the test for whether a shape
// reaches a face; extents never are.
[[nodiscard]] bool PolygonsOverlap2D(std::span<const Vec2d> a, std::span<const Vec2d> b, float tolerance);

// The point where two segments properly cross, or nullopt when they do not
// (a touch or a shared endpoint is not a crossing).
[[nodiscard]] std::optional<Vec2d> SegmentCrossing2D(Vec2d a, Vec2d b, Vec2d c, Vec2d d, float tolerance);

// Whether `inner` lies within `outer`, touching allowed. Exact rather than
// sampled: each inner edge is cut at every point where it meets the outer
// boundary, and every resulting sub-interval is classified, so an edge that
// slips out through a reflex vertex and back is caught.
[[nodiscard]] bool PolygonContainsPolygon2D(std::span<const Vec2d> outer, std::span<const Vec2d> inner,
                                            float tolerance);

struct SurroundResult
{
    CarveStatus Status = CarveStatus::Ok;
    std::vector<std::vector<Vec2d>> Pieces; // empty unless Status is Ok
};

// A vertex of a piece a line split off a polygon: an input vertex by index,
// whose position is the input's verbatim, or a crossing on input edge
// `Edge -> Edge + 1`, interpolated. The provenance is what lets a caller keep
// pre-existing 3D vertices exactly and compute only the crossings anew.
inline constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;
struct SplitVertex2D
{
    std::uint32_t Source = kNoSource;
    std::uint32_t Edge = kNoSource;
    Vec2d Position;
};

struct PolygonSplit2D
{
    CarveStatus Status = CarveStatus::Ok;
    std::vector<std::vector<SplitVertex2D>> Left;  // the side the line's left normal points to
    std::vector<std::vector<SplitVertex2D>> Right;
    // The line's runs through the polygon's interior, one per interior span:
    // the segments a cap needs. Endpoints are the crossings they join.
    std::vector<std::pair<SplitVertex2D, SplitVertex2D>> Spans;
};

// The pieces of a simple counter-clockwise polygon on each side of a line, each
// simple and counter-clockwise, with the spans the line draws through the
// interior. Vertices within `onLineTolerance` of the line are on it, and
// consecutive on-line vertices are one event judged by the nearest off-line
// vertices either side: same side is a contact (no crossing, no span),
// opposite sides a crossing. An edge lying on the line is therefore never
// bridged over. A polygon whose vertices are all on the line is refused
// (InvalidOutline).
//
// Two tolerances, because they answer different questions. The on-line one is
// a snap: how far from the line a vertex may sit and still count as on it. The
// interior one is a predicate epsilon for deciding whether the stretch between
// two consecutive events runs through the polygon's interior: its midpoint is
// never on an edge (an edge on the line is one event), so it only has to tell
// inside from outside, and a corner cut off at the snap height must read as
// inside. Handing it the snap tolerance loses exactly those spans.
[[nodiscard]] PolygonSplit2D SplitPolygonByLine2D(std::span<const Vec2d> polygon, Vec2d pointOnLine,
                                                  Vec2d direction, float onLineTolerance,
                                                  float interiorTolerance);

// The same, with each vertex's signed distance to the line supplied (positive
// on the left), for a caller that classified the vertices elsewhere -- a solid's
// vertices against a plane, once, so every face agrees on which are on it.
// `direction` orders the crossings along the line.
[[nodiscard]] PolygonSplit2D SplitPolygonByDistances2D(std::span<const Vec2d> polygon,
                                                       std::span<const float> distances, Vec2d direction,
                                                       float onLineTolerance, float interiorTolerance);

// The pieces of `outer` with several disjoint `holes` removed, all simple and
// counter-clockwise. Holes are bridged one at a time into the piece that
// contains them, in an order and a direction whose bridges cross no hole not
// yet bridged; an arrangement with no such order refuses (ChannelCrossesHole).
[[nodiscard]] SurroundResult SurroundPolygonsWithHoles(std::span<const Vec2d> outer,
                                                       std::span<const std::vector<Vec2d>> holes,
                                                       float tolerance);

// The pieces of `outer` with `hole` removed, both simple and counter-clockwise.
//
// One traversal covers every arrangement. Each ring is first split at the
// other's vertices lying inside one of its edges -- bidirectionally, because a
// single hole edge can span several outer edges where an earlier carve already
// subdivided the boundary. The output boundary is then the outer edges the hole
// does not share, the reversed hole edges the outer does not share, and, when
// the hole touches nothing, two bridges joining them; faces are traced from
// that arc set by the standard rotational rule.
//
// A hole touching the rim along one run yields one piece, along two runs yields
// two, and a hole meeting the rim at a single point yields two pieces sharing
// that vertex, which is legal. A hole strictly inside yields two pieces split
// by the bridges: a single bridge would make one piece with a doubled edge,
// which the tessellator silently mis-triangulates.
[[nodiscard]] SurroundResult SurroundPolygons(std::span<const Vec2d> outer, std::span<const Vec2d> hole,
                                              float tolerance);

