// The carve's 2D half. These run without a mesh on purpose: the surround walk
// is where the operation is most likely to be wrong, and a failure here should
// point at the geometry rather than at whatever the mesh did with it.
#include "brush/CarveSurround.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace
{
using Poly = std::vector<Vec2d>;
constexpr float kTol = 1e-4f;
// The split's interior predicate epsilon: inside or outside for a midpoint
// that is never on an edge, so far below the snap.
constexpr float kInterior = 1e-6f;

Vec2d P(float x, float y) { return Vec2d{ x, y }; }

const Poly kWall = { P(0, 0), P(4, 0), P(4, 3), P(0, 3) };
// An L: concave, so the walk cannot lean on a rectangular host.
const Poly kConcave = { P(0, 0), P(4, 0), P(4, 1), P(2, 1), P(2, 3), P(0, 3) };

float Cross(Vec2d o, Vec2d a, Vec2d b)
{
    return (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);
}

bool OnSegment(Vec2d p, Vec2d a, Vec2d b)
{
    if (std::abs(Cross(a, b, p)) > 1e-4f)
        return false;
    return p.X >= std::min(a.X, b.X) - 1e-4f && p.X <= std::max(a.X, b.X) + 1e-4f
        && p.Y >= std::min(a.Y, b.Y) - 1e-4f && p.Y <= std::max(a.Y, b.Y) + 1e-4f;
}

bool SegmentsCross(Vec2d a, Vec2d b, Vec2d c, Vec2d d)
{
    const float d1 = Cross(c, d, a);
    const float d2 = Cross(c, d, b);
    const float d3 = Cross(a, b, c);
    const float d4 = Cross(a, b, d);
    if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)))
        return true;
    return OnSegment(a, c, d) || OnSegment(b, c, d) || OnSegment(c, a, b) || OnSegment(d, a, b);
}

bool IsSimple(const Poly& poly)
{
    const std::size_t n = poly.size();
    if (n < 3)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            if (i != j && std::abs(poly[i].X - poly[j].X) <= kTol && std::abs(poly[i].Y - poly[j].Y) <= kTol)
                return false;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
        {
            if (j == i || (j + 1) % n == i || (i + 1) % n == j)
                continue;
            if (SegmentsCross(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n]))
                return false;
        }
    return true;
}

// Every invariant the surround owes its caller, in one place so the hand-written
// cases and the property sweep check exactly the same thing.
void ExpectSound(const SurroundResult& result, const Poly& outer, const Poly& hole)
{
    ASSERT_EQ(result.Status, CarveStatus::Ok) << CarveStatusText(result.Status);
    ASSERT_FALSE(result.Pieces.empty());
    float total = 0.0f;
    for (const Poly& piece : result.Pieces)
    {
        EXPECT_TRUE(IsSimple(piece)) << "a surround piece is not a simple polygon";
        const float area = PolygonSignedArea(piece);
        EXPECT_GT(area, 0.0f) << "a surround piece is wound clockwise";
        for (const Vec2d& p : piece)
            EXPECT_NE(ClassifyPointInPolygon2D(outer, p, kTol), PointPolygonRelation::Outside)
                << "a surround piece escapes the host face";
        total += area;
    }
    EXPECT_NEAR(total + PolygonSignedArea(hole), PolygonSignedArea(outer), 1e-3f)
        << "surround plus hole must be the host";
}
}

TEST(CarveSurround, DoorwayFlushWithTheRimIsOneFace)
{
    // The point of the whole change: a carve touching the rim leaves one face
    // with no edges radiating from its corners.
    const Poly hole = { P(1, 0), P(3, 0), P(3, 2), P(1, 2) };
    const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
    ExpectSound(result, kWall, hole);
    EXPECT_EQ(result.Pieces.size(), 1u);
}

TEST(CarveSurround, SpanningOppositeRimsGivesTwoFaces)
{
    const Poly hole = { P(0, 1), P(4, 1), P(4, 2), P(0, 2) };
    const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
    ExpectSound(result, kWall, hole);
    EXPECT_EQ(result.Pieces.size(), 2u);
}

TEST(CarveSurround, TwoAdjacentRimsIsStillOneFace)
{
    const Poly hole = { P(0, 0), P(2, 0), P(2, 2), P(0, 2) };
    const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
    ExpectSound(result, kWall, hole);
    EXPECT_EQ(result.Pieces.size(), 1u);
}

TEST(CarveSurround, WindowStrictlyInsideIsBridgedIntoTwoFaces)
{
    // A single bridge would leave one face with a doubled edge, which the
    // tessellator mis-triangulates without saying so; two is the honest cost.
    const Poly hole = { P(1, 1), P(3, 1), P(3, 2), P(1, 2) };
    const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
    ExpectSound(result, kWall, hole);
    EXPECT_EQ(result.Pieces.size(), 2u);
}

TEST(CarveSurround, PointContactSplitsRatherThanPinching)
{
    // An apex meeting the rim at one vertex: two proper faces sharing it, not
    // one loop that visits the vertex twice.
    const Poly hole = { P(1, 0), P(3, 0), P(2, 3) };
    const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
    ExpectSound(result, kWall, hole);
    EXPECT_EQ(result.Pieces.size(), 2u);
}

TEST(CarveSurround, HoleVertexOnAHostVertex)
{
    const Poly hole = { P(0, 0), P(2, 0), P(2, 2) };
    const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
    ExpectSound(result, kWall, hole);
}

TEST(CarveSurround, HoleEdgeSpanningAnAlreadySubdividedRim)
{
    // The case that forced bidirectional contact insertion: one hole edge runs
    // the length of three host edges, so nothing is shared until the hole is
    // split too. This is a second carve landing flush beside an earlier one.
    const Poly outer = { P(0, 0), P(1, 0), P(3, 0), P(4, 0), P(4, 2), P(0, 2) };
    const Poly hole = { P(0.5f, 0), P(3.5f, 0), P(3.5f, 1), P(0.5f, 1) };
    const SurroundResult result = SurroundPolygons(outer, hole, kTol);
    ExpectSound(result, outer, hole);
    EXPECT_EQ(result.Pieces.size(), 1u);
}

TEST(CarveSurround, ConcaveHostFlushAndWindowed)
{
    const Poly flush = { P(0, 0), P(1, 0), P(1, 1), P(0, 1) };
    const SurroundResult flushed = SurroundPolygons(kConcave, flush, kTol);
    ExpectSound(flushed, kConcave, flush);
    EXPECT_EQ(flushed.Pieces.size(), 1u);

    const Poly window = { P(0.5f, 1.5f), P(1.5f, 1.5f), P(1.5f, 2.5f), P(0.5f, 2.5f) };
    const SurroundResult windowed = SurroundPolygons(kConcave, window, kTol);
    ExpectSound(windowed, kConcave, window);
    EXPECT_EQ(windowed.Pieces.size(), 2u);
}

TEST(CarveSurround, RefusesInputItCannotWalk)
{
    const Poly tooFewPoints = { P(0, 0), P(1, 0) };
    EXPECT_EQ(SurroundPolygons(kWall, tooFewPoints, kTol).Status, CarveStatus::InvalidOutline);
    // Clockwise: the caller owes a CCW outline, and guessing would flip the
    // material side.
    const Poly wound = { P(1, 1), P(1, 2), P(3, 2), P(3, 1) };
    EXPECT_EQ(SurroundPolygons(kWall, wound, kTol).Status, CarveStatus::InvalidOutline);
}

TEST(CarveSurround, PointClassificationSeparatesBoundaryFromInside)
{
    EXPECT_EQ(ClassifyPointInPolygon2D(kConcave, P(0.5f, 0.5f), kTol), PointPolygonRelation::Inside);
    EXPECT_EQ(ClassifyPointInPolygon2D(kConcave, P(3, 2), kTol), PointPolygonRelation::Outside);
    // The reflex notch: inside the bounding box, outside the polygon. This is
    // the case a triangle fan from vertex zero gets wrong.
    EXPECT_EQ(ClassifyPointInPolygon2D(kConcave, P(3, 2.5f), kTol), PointPolygonRelation::Outside);
    EXPECT_EQ(ClassifyPointInPolygon2D(kConcave, P(2, 2), kTol), PointPolygonRelation::Boundary);
    EXPECT_EQ(ClassifyPointInPolygon2D(kConcave, P(0, 0), kTol), PointPolygonRelation::Boundary);
    EXPECT_EQ(ClassifyPointInPolygon2D(kConcave, P(1, 1e-5f), kTol), PointPolygonRelation::Boundary);
}

TEST(CarveSurround, PropertySweepOverGeneratedHoles)
{
    // Deterministic seeds: a failure here is reproducible by its index. The
    // sweep covers placements no hand-written case would think to try, and
    // checks exactly the invariants ExpectSound states.
    std::mt19937 rng(20260910u);
    std::uniform_real_distribution<float> coord(0.0f, 4.0f);
    int walked = 0;
    for (int i = 0; i < 400; ++i)
    {
        const float x0 = coord(rng);
        const float x1 = coord(rng);
        const float y0 = coord(rng) * 0.75f;
        const float y1 = coord(rng) * 0.75f;
        const float lo = std::min(x0, x1);
        const float hi = std::max(x0, x1);
        const float bottom = std::min(y0, y1);
        const float top = std::max(y0, y1);
        if (hi - lo < 0.2f || top - bottom < 0.2f)
            continue;
        const Poly hole = { P(lo, bottom), P(hi, bottom), P(hi, top), P(lo, top) };
        const SurroundResult result = SurroundPolygons(kWall, hole, kTol);
        if (result.Status != CarveStatus::Ok)
            continue; // a refusal is a legitimate answer; soundness is about what it returns
        ++walked;
        ExpectSound(result, kWall, hole);
    }
    EXPECT_GT(walked, 100) << "the sweep refused nearly everything; it is not exercising the walk";
}

TEST(CarveSurround, SimplicityRejectsWhatWouldMisTessellate)
{
    EXPECT_TRUE(IsSimplePolygon2D(kWall, kTol));
    EXPECT_TRUE(IsSimplePolygon2D(kConcave, kTol));

    const Poly bowtie = { P(0, 0), P(2, 2), P(2, 0), P(0, 2) };
    EXPECT_FALSE(IsSimplePolygon2D(bowtie, kTol));

    // A repeated vertex is the keyhole slit the ear clipper silently fans and
    // repair does not catch, so it has to be refused up front.
    const Poly repeated = { P(0, 0), P(2, 0), P(2, 2), P(1, 1), P(0, 2), P(1, 1) };
    EXPECT_FALSE(IsSimplePolygon2D(repeated, kTol));

    const Poly backtrack = { P(0, 0), P(2, 0), P(1, 0), P(1, 2) };
    EXPECT_FALSE(IsSimplePolygon2D(backtrack, kTol));

    const Poly touching = { P(0, 0), P(4, 0), P(4, 3), P(2, 1), P(0, 3) };
    EXPECT_TRUE(IsSimplePolygon2D(touching, kTol)) << "a reflex vertex is not a self-intersection";

    EXPECT_FALSE(IsSimplePolygon2D(std::vector<Vec2d>{ P(0, 0), P(1, 0) }, kTol));
}

TEST(CarveSurround, ContainmentAllowsTouchingAndCatchesAnExcursion)
{
    EXPECT_TRUE(PolygonContainsPolygon2D(kWall, Poly{ P(1, 1), P(3, 1), P(3, 2), P(1, 2) }, kTol));
    EXPECT_TRUE(PolygonContainsPolygon2D(kWall, Poly{ P(1, 0), P(3, 0), P(3, 2), P(1, 2) }, kTol))
        << "a flush carve touches the rim and is still inside";
    EXPECT_TRUE(PolygonContainsPolygon2D(kWall, kWall, kTol)) << "the face contains itself";

    EXPECT_FALSE(PolygonContainsPolygon2D(kWall, Poly{ P(3, 1), P(5, 1), P(5, 2), P(3, 2) }, kTol));
    EXPECT_FALSE(PolygonContainsPolygon2D(kWall, Poly{ P(1, 1), P(5, 1), P(5, 2), P(1, 2) }, kTol));
}

TEST(CarveSurround, ContainmentCatchesAnEdgeLeavingThroughTwoVertices)
{
    // A square with a triangular notch bitten out of its top edge. The inner
    // triangle's top edge runs along y = 4 from the right corner to a point on
    // the intact part of the top edge, so it passes over the notch: outside the
    // face, but touching the boundary only at the notch's two mouth vertices.
    // No vertex is outside and no edge properly crosses another, and the
    // straddling edge's own midpoint lands back on solid boundary, so this is
    // exactly the case a vertex or midpoint test waves through.
    const Poly notched = { P(0, 0),   P(4, 0), P(4, 4), P(2.5f, 4),
                           P(2, 2), P(1.5f, 4), P(0, 4) };
    const Poly straddling = { P(2, 0.5f), P(4, 4), P(1.4f, 4) };

    for (const Vec2d& p : straddling)
        EXPECT_NE(ClassifyPointInPolygon2D(notched, p, kTol), PointPolygonRelation::Outside);
    EXPECT_NE(ClassifyPointInPolygon2D(notched, P(2.7f, 4), kTol), PointPolygonRelation::Outside)
        << "the straddling edge's midpoint is on the boundary, not outside";

    EXPECT_FALSE(PolygonContainsPolygon2D(notched, straddling, kTol));
    EXPECT_TRUE(PolygonContainsPolygon2D(notched, Poly{ P(0.5f, 0.5f), P(3.5f, 0.5f), P(3.5f, 1.5f),
                                                        P(0.5f, 1.5f) }, kTol));
}

TEST(CarveSurround, ClipKeepsAPolygonAlreadyInside)
{
    const Poly inner = { P(1, 1), P(3, 1), P(3, 2), P(1, 2) };
    const Poly out = ClipPolygonToRect2D(inner, P(0, 0), P(4, 3), kTol);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_NEAR(std::abs(PolygonSignedArea(out)), 2.0f, kTol);
}

TEST(CarveSurround, ClipAgainstOneLineAndACorner)
{
    // Straddling the right edge: half stays.
    const Poly across = { P(3, 1), P(5, 1), P(5, 2), P(3, 2) };
    const Poly half = ClipPolygonToRect2D(across, P(0, 0), P(4, 3), kTol);
    EXPECT_NEAR(std::abs(PolygonSignedArea(half)), 1.0f, kTol);
    for (const Vec2d& p : half)
        EXPECT_LE(p.X, 4.0f + kTol);

    // Straddling the top-right corner: a quarter stays and the corner is exact.
    const Poly corner = { P(3, 2), P(5, 2), P(5, 4), P(3, 4) };
    const Poly quarter = ClipPolygonToRect2D(corner, P(0, 0), P(4, 3), kTol);
    EXPECT_NEAR(std::abs(PolygonSignedArea(quarter)), 1.0f, kTol);
    bool hasCorner = false;
    for (const Vec2d& p : quarter)
        hasCorner = hasCorner || (p.X == 4.0f && p.Y == 3.0f);
    EXPECT_TRUE(hasCorner);
}

TEST(CarveSurround, ClipOfAContactOrAMissIsEmpty)
{
    const Poly touching = { P(4, 1), P(6, 1), P(6, 2), P(4, 2) };
    EXPECT_TRUE(ClipPolygonToRect2D(touching, P(0, 0), P(4, 3), kTol).empty());
    const Poly away = { P(5, 1), P(6, 1), P(6, 2), P(5, 2) };
    EXPECT_TRUE(ClipPolygonToRect2D(away, P(0, 0), P(4, 3), kTol).empty());
}

TEST(CarveSurround, AChordSplitsAnArchIntoRectangleAndSegment)
{
    // An arch: jambs to y = 2, then a three-segment arc to the apex at y = 3.
    const Poly arch = { P(1, 0), P(3, 0), P(3, 2), P(2.7f, 2.7f), P(2, 3), P(1.3f, 2.7f), P(1, 2) };
    const Poly below = ClipPolygonToRect2D(arch, P(0, 0), P(4, 2), kTol);
    const Poly above = ClipPolygonToRect2D(arch, P(0, 2), P(4, 3), kTol);
    ASSERT_EQ(below.size(), 4u) << "the lower part is the rectangle under the springline";
    EXPECT_NEAR(std::abs(PolygonSignedArea(below)), 4.0f, kTol);
    ASSERT_EQ(above.size(), 5u) << "the upper part is the arc on its chord";
    // The chord endpoints are the springline vertices exactly, not a step short.
    int onChord = 0;
    for (const Vec2d& p : above)
        if (p.Y == 2.0f)
            ++onChord;
    EXPECT_EQ(onChord, 2);
    EXPECT_NEAR(std::abs(PolygonSignedArea(below)) + std::abs(PolygonSignedArea(above)),
                std::abs(PolygonSignedArea(arch)), kTol);
}

namespace
{
float PieceArea(const std::vector<SplitVertex2D>& piece)
{
    Poly ring;
    for (const SplitVertex2D& v : piece)
        ring.push_back(v.Position);
    return PolygonSignedArea(ring);
}

void ExpectSplitSound(const PolygonSplit2D& split, const Poly& polygon)
{
    ASSERT_EQ(split.Status, CarveStatus::Ok) << CarveStatusText(split.Status);
    float total = 0.0f;
    for (const auto* side : { &split.Left, &split.Right })
        for (const std::vector<SplitVertex2D>& piece : *side)
        {
            Poly ring;
            for (const SplitVertex2D& v : piece)
            {
                ring.push_back(v.Position);
                if (v.Source != kNoSource)
                {
                    EXPECT_EQ(polygon[v.Source].X, v.Position.X) << "a source vertex moved";
                }
            }
            EXPECT_TRUE(IsSimplePolygon2D(ring, kTol));
            EXPECT_GT(PolygonSignedArea(ring), 0.0f) << "a piece is not counter-clockwise";
            total += PolygonSignedArea(ring);
        }
    EXPECT_NEAR(total, PolygonSignedArea(polygon), 1e-3f) << "the pieces do not add up to the polygon";
}
}

TEST(CarveSurround, SplitAConvexPolygonByALine)
{
    const PolygonSplit2D split = SplitPolygonByLine2D(kWall, P(0, 1.5f), P(1, 0), kTol, kInterior);
    ExpectSplitSound(split, kWall);
    EXPECT_EQ(split.Left.size(), 1u);
    EXPECT_EQ(split.Right.size(), 1u);
    EXPECT_EQ(split.Spans.size(), 1u);
    EXPECT_NEAR(PieceArea(split.Left.front()), 6.0f, kTol);
    // The crossings are new and carry the edge they lie on.
    int crossings = 0;
    for (const SplitVertex2D& v : split.Left.front())
        if (v.Source == kNoSource)
        {
            ++crossings;
            EXPECT_NE(v.Edge, kNoSource);
        }
    EXPECT_EQ(crossings, 2);
}

TEST(CarveSurround, SplitAUShapeCrossedFourTimes)
{
    // A U open at the top: legs x in [0,1] and [3,4], base y in [0,1].
    const Poly u = { P(0, 0), P(4, 0), P(4, 3), P(3, 3), P(3, 1), P(1, 1), P(1, 3), P(0, 3) };
    const PolygonSplit2D split = SplitPolygonByLine2D(u, P(0, 2), P(1, 0), kTol, kInterior);
    ExpectSplitSound(split, u);
    EXPECT_EQ(split.Left.size(), 2u) << "the two legs above the line";
    EXPECT_EQ(split.Right.size(), 1u) << "the base below";
    EXPECT_EQ(split.Spans.size(), 2u) << "one span per leg, none across the opening";
}

TEST(CarveSurround, SplitACShapeCrossedSixTimes)
{
    // An E without its middle bar: three prongs pointing +x, a spine on the left.
    const Poly c = { P(0, 0), P(4, 0), P(4, 1), P(1, 1), P(1, 2), P(4, 2), P(4, 3), P(1, 3), P(1, 4), P(4, 4), P(4, 5), P(0, 5) };
    const PolygonSplit2D split = SplitPolygonByLine2D(c, P(2, 0), P(0, 1), kTol, kInterior);
    ExpectSplitSound(split, c);
    EXPECT_EQ(split.Spans.size(), 3u);
    EXPECT_EQ(split.Right.size(), 3u) << "the three prong tips";
    EXPECT_EQ(split.Left.size(), 1u);
}

TEST(CarveSurround, SplitThroughAVertexIsOneCrossing)
{
    // A diamond cut horizontally through its left and right vertices.
    const Poly diamond = { P(2, 0), P(4, 2), P(2, 4), P(0, 2) };
    const PolygonSplit2D split = SplitPolygonByLine2D(diamond, P(0, 2), P(1, 0), kTol, kInterior);
    ExpectSplitSound(split, diamond);
    EXPECT_EQ(split.Left.size(), 1u);
    EXPECT_EQ(split.Right.size(), 1u);
    EXPECT_EQ(split.Spans.size(), 1u);
    for (const auto* side : { &split.Left, &split.Right })
        for (const SplitVertex2D& v : side->front())
            EXPECT_NE(v.Source, kNoSource) << "no new vertex: the cut passes through existing ones";
    EXPECT_EQ(split.Left.front().size(), 3u);
}

TEST(CarveSurround, ATangentVertexIsNotACrossing)
{
    const Poly diamond = { P(2, 0), P(4, 2), P(2, 4), P(0, 2) };
    const PolygonSplit2D split = SplitPolygonByLine2D(diamond, P(0, 0), P(1, 0), kTol, kInterior); // touches at (2, 0)
    ExpectSplitSound(split, diamond);
    EXPECT_EQ(split.Left.size(), 1u);
    EXPECT_TRUE(split.Right.empty());
    EXPECT_TRUE(split.Spans.empty());
    EXPECT_EQ(split.Left.front().size(), 4u);
}

TEST(CarveSurround, AnEdgeOnTheLineIsNeverBridged)
{
    // The line runs along the wall's bottom edge: the wall is one piece, whole.
    const PolygonSplit2D along = SplitPolygonByLine2D(kWall, P(0, 0), P(1, 0), kTol, kInterior);
    ExpectSplitSound(along, kWall);
    EXPECT_EQ(along.Left.size(), 1u);
    EXPECT_TRUE(along.Right.empty());
    EXPECT_TRUE(along.Spans.empty());

    // A notch whose floor lies on the line: the run is a crossing (the
    // polygon is above on one side of it and dips below on the other), and
    // the span never doubles the notch floor.
    const Poly notched = { P(0, 0), P(4, 0), P(4, 3), P(3, 3), P(3, 1), P(1, 1), P(1, 3), P(0, 3) };
    const PolygonSplit2D split = SplitPolygonByLine2D(notched, P(0, 1), P(1, 0), kTol, kInterior);
    ExpectSplitSound(split, notched);
    EXPECT_EQ(split.Left.size(), 2u);
    EXPECT_EQ(split.Right.size(), 1u);
    // The base's top is the two leg widths bridged, and the notch floor itself
    // between them: a boundary, never doubled by a span.
    EXPECT_EQ(split.Spans.size(), 2u);
    for (const auto& [a, b] : split.Spans)
        EXPECT_NEAR(std::abs(b.Position.X - a.Position.X), 1.0f, kTol);
}

TEST(CarveSurround, APolygonWhollyOnOneSideIsOnePiece)
{
    const PolygonSplit2D split = SplitPolygonByLine2D(kWall, P(0, -1), P(1, 0), kTol, kInterior);
    ExpectSplitSound(split, kWall);
    EXPECT_EQ(split.Left.size(), 1u);
    EXPECT_TRUE(split.Right.empty());
    EXPECT_TRUE(split.Spans.empty());
}

TEST(CarveSurround, SeveralHolesAreBridgedWithoutCrossingEachOther)
{
    const Poly outer = { P(0, 0), P(8, 0), P(8, 4), P(0, 4) };
    const Poly left = { P(1, 1), P(3, 1), P(3, 3), P(1, 3) };
    const Poly right = { P(5, 1), P(7, 1), P(7, 3), P(5, 3) };
    const Poly top = { P(3.5f, 3.2f), P(4.5f, 3.2f), P(4.5f, 3.8f), P(3.5f, 3.8f) };
    for (const std::vector<Poly>& holes : { std::vector<Poly>{ left, right }, std::vector<Poly>{ left, right, top } })
    {
        const SurroundResult result = SurroundPolygonsWithHoles(outer, holes, kTol);
        ASSERT_EQ(result.Status, CarveStatus::Ok) << CarveStatusText(result.Status);
        float area = 0.0f;
        for (const Poly& piece : result.Pieces)
        {
            EXPECT_TRUE(IsSimplePolygon2D(piece, kTol));
            EXPECT_GT(PolygonSignedArea(piece), 0.0f);
            area += PolygonSignedArea(piece);
            for (const Poly& hole : holes)
                EXPECT_FALSE(PolygonsOverlap2D(piece, hole, kTol)) << "a piece covers a hole";
        }
        float expected = PolygonSignedArea(outer);
        for (const Poly& hole : holes)
            expected -= PolygonSignedArea(hole);
        EXPECT_NEAR(area, expected, 1e-3f);
    }
}

TEST(CarveSurroundSplit, ACornerCutOffAtTheSnapHeightIsStillAPiece)
{
    // The corner of an arch cell (a quad with one acute corner at the crown),
    // split 2e-4 inside that corner: the sliver triangle is a piece and the
    // span across it exists. The span's midpoint lies 6e-5 from the boundary,
    // so the interior predicate must not inherit the snap tolerance, or the
    // midpoint reads as boundary and the span is lost -- the second case is
    // the defect the clip kernel had.
    const std::vector<Vec2d> cell{ P(0, 0.5f), P(-1, 0.5f), P(-1, -1), P(-0.7071f, 0.0607f) };
    const PolygonSplit2D split = SplitPolygonByLine2D(cell, P(-2e-4f, 0), P(0, 1), kTol, kInterior);
    ASSERT_EQ(split.Status, CarveStatus::Ok);
    EXPECT_EQ(split.Spans.size(), 1u);
    EXPECT_EQ(split.Left.size() + split.Right.size(), 2u);

    const PolygonSplit2D wrong = SplitPolygonByLine2D(cell, P(-2e-4f, 0), P(0, 1), kTol, kTol);
    EXPECT_TRUE(wrong.Spans.empty()) << "the snap tolerance as the interior epsilon loses the span";
}
