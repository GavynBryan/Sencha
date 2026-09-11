#include "brush/CarveSurround.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace
{
// Distance from `p` to segment [a, b], clamped to the ends.
float DistanceToSegment(Vec2d p, Vec2d a, Vec2d b)
{
    const Vec2d ab{ b.X - a.X, b.Y - a.Y };
    const float lenSq = ab.X * ab.X + ab.Y * ab.Y;
    if (lenSq <= 0.0f)
        return std::sqrt((p.X - a.X) * (p.X - a.X) + (p.Y - a.Y) * (p.Y - a.Y));
    float t = ((p.X - a.X) * ab.X + (p.Y - a.Y) * ab.Y) / lenSq;
    t = std::clamp(t, 0.0f, 1.0f);
    const Vec2d q{ a.X + ab.X * t, a.Y + ab.Y * t };
    return std::sqrt((p.X - q.X) * (p.X - q.X) + (p.Y - q.Y) * (p.Y - q.Y));
}

bool NearlyEqual(Vec2d a, Vec2d b, float tolerance)
{
    return std::abs(a.X - b.X) <= tolerance && std::abs(a.Y - b.Y) <= tolerance;
}

// Parameter of `p` along [a, b], for ordering points inserted into one edge.
float ParameterOn(Vec2d a, Vec2d b, Vec2d p)
{
    const Vec2d ab{ b.X - a.X, b.Y - a.Y };
    const float lenSq = ab.X * ab.X + ab.Y * ab.Y;
    if (lenSq <= 0.0f)
        return 0.0f;
    return ((p.X - a.X) * ab.X + (p.Y - a.Y) * ab.Y) / lenSq;
}

// Inserts every point of `points` that lies strictly inside one of `ring`'s
// edges, keeping each edge's insertions ordered along it.
std::vector<Vec2d> SplitRing(std::span<const Vec2d> ring, std::span<const Vec2d> points, float tolerance)
{
    std::vector<Vec2d> out;
    out.reserve(ring.size() + points.size());
    for (std::size_t i = 0; i < ring.size(); ++i)
    {
        const Vec2d a = ring[i];
        const Vec2d b = ring[(i + 1) % ring.size()];
        out.push_back(a);

        std::vector<std::pair<float, Vec2d>> inserted;
        for (const Vec2d& p : points)
        {
            if (NearlyEqual(p, a, tolerance) || NearlyEqual(p, b, tolerance))
                continue;
            if (DistanceToSegment(p, a, b) > tolerance)
                continue;
            const float t = ParameterOn(a, b, p);
            if (t <= 0.0f || t >= 1.0f)
                continue;
            inserted.emplace_back(t, p);
        }
        std::sort(inserted.begin(), inserted.end(),
                  [](const auto& l, const auto& r) { return l.first < r.first; });
        for (const auto& [t, p] : inserted)
            if (out.empty() || !NearlyEqual(out.back(), p, tolerance))
                out.push_back(p);
    }
    return out;
}

// A pool that collapses near-equal points onto one index, so every comparison
// after this is an integer one. Float positions cannot carry the arc bookkeeping
// directly: two points a tolerance apart have to be the same corner or the
// traversal loses an edge.
class PointPool
{
public:
    explicit PointPool(float tolerance) : Tolerance(tolerance) {}

    std::uint32_t Intern(Vec2d p)
    {
        for (std::uint32_t i = 0; i < Points.size(); ++i)
            if (NearlyEqual(Points[i], p, Tolerance))
                return i;
        Points.push_back(p);
        return static_cast<std::uint32_t>(Points.size() - 1);
    }

    [[nodiscard]] Vec2d At(std::uint32_t i) const { return Points[i]; }

private:
    float Tolerance;
    std::vector<Vec2d> Points;
};

struct Bridge
{
    Vec2d OuterPoint;
    Vec2d HolePoint;
};

// Casts from `from` along +/-U and returns where it first meets the ring.
std::optional<Vec2d> CastToRing(std::span<const Vec2d> ring, Vec2d from, float directionX, float tolerance)
{
    float best = std::numeric_limits<float>::max();
    std::optional<Vec2d> hit;
    for (std::size_t i = 0; i < ring.size(); ++i)
    {
        const Vec2d a = ring[i];
        const Vec2d b = ring[(i + 1) % ring.size()];
        const float dy = b.Y - a.Y;
        if (std::abs(dy) <= tolerance)
            continue; // parallel to the cast: any crossing is the ring running alongside
        const float s = (from.Y - a.Y) / dy;
        if (s < 0.0f || s > 1.0f)
            continue;
        const float x = a.X + (b.X - a.X) * s;
        const float travel = (x - from.X) * directionX;
        if (travel <= tolerance)
            continue; // behind the start, or the start itself
        if (travel < best)
        {
            best = travel;
            hit = Vec2d{ x, from.Y };
        }
    }
    return hit;
}

// The two bridges for a hole that touches nothing. They leave the hole's
// opposite extremes in opposite directions, so their U ranges are disjoint and
// they provably cannot cross; each stops at the ring's first crossing, so it
// stays inside.
std::optional<std::array<Bridge, 2>> BuildBridges(std::span<const Vec2d> outer, std::span<const Vec2d> hole,
                                                  float tolerance)
{
    std::size_t maxU = 0;
    std::size_t minU = 0;
    for (std::size_t i = 1; i < hole.size(); ++i)
    {
        const auto better = [&](std::size_t candidate, std::size_t incumbent, bool wantMax) {
            const Vec2d c = hole[candidate];
            const Vec2d in = hole[incumbent];
            if (std::abs(c.X - in.X) > tolerance)
                return wantMax ? c.X > in.X : c.X < in.X;
            return c.Y < in.Y; // deterministic tie-break
        };
        if (better(i, maxU, /*wantMax*/ true))
            maxU = i;
        if (better(i, minU, /*wantMax*/ false))
            minU = i;
    }
    if (NearlyEqual(hole[maxU], hole[minU], tolerance))
        return std::nullopt; // no width to separate the bridges

    const std::optional<Vec2d> right = CastToRing(outer, hole[maxU], 1.0f, tolerance);
    const std::optional<Vec2d> left = CastToRing(outer, hole[minU], -1.0f, tolerance);
    if (!right.has_value() || !left.has_value())
        return std::nullopt;
    return std::array<Bridge, 2>{ Bridge{ *right, hole[maxU] }, Bridge{ *left, hole[minU] } };
}
}

float PolygonSignedArea(std::span<const Vec2d> polygon)
{
    float sum = 0.0f;
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const Vec2d a = polygon[i];
        const Vec2d b = polygon[(i + 1) % polygon.size()];
        sum += a.X * b.Y - b.X * a.Y;
    }
    return sum * 0.5f;
}

PointPolygonRelation ClassifyPointInPolygon2D(std::span<const Vec2d> polygon, Vec2d point, float tolerance)
{
    if (polygon.size() < 3)
        return PointPolygonRelation::Outside;

    for (std::size_t i = 0; i < polygon.size(); ++i)
        if (DistanceToSegment(point, polygon[i], polygon[(i + 1) % polygon.size()]) <= tolerance)
            return PointPolygonRelation::Boundary;

    // Crossing number: count ring edges passing the ray to +X. The boundary is
    // already handled, so the ray's exact tie behaviour cannot misclassify a
    // point that sits on an edge.
    bool inside = false;
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const Vec2d a = polygon[i];
        const Vec2d b = polygon[(i + 1) % polygon.size()];
        if ((a.Y > point.Y) == (b.Y > point.Y))
            continue;
        const float x = a.X + (point.Y - a.Y) / (b.Y - a.Y) * (b.X - a.X);
        if (x > point.X)
            inside = !inside;
    }
    return inside ? PointPolygonRelation::Inside : PointPolygonRelation::Outside;
}

SurroundResult SurroundPolygons(std::span<const Vec2d> outer, std::span<const Vec2d> hole, float tolerance)
{
    if (outer.size() < 3 || hole.size() < 3)
        return { CarveStatus::InvalidOutline, {} };
    if (PolygonSignedArea(outer) <= 0.0f || PolygonSignedArea(hole) <= 0.0f)
        return { CarveStatus::InvalidOutline, {} };

    // Bridges are found against the original ring, then their endpoints join it
    // like any other contact point.
    std::vector<Vec2d> bridgePoints;
    std::array<Bridge, 2> bridges{};
    bool bridged = false;
    {
        bool touches = false;
        for (const Vec2d& h : hole)
            if (ClassifyPointInPolygon2D(outer, h, tolerance) == PointPolygonRelation::Boundary)
            {
                touches = true;
                break;
            }
        if (!touches)
        {
            const std::optional<std::array<Bridge, 2>> built = BuildBridges(outer, hole, tolerance);
            if (!built.has_value())
                return { CarveStatus::InvalidOutline, {} };
            bridges = *built;
            bridged = true;
            bridgePoints = { bridges[0].OuterPoint, bridges[1].OuterPoint };
        }
    }

    std::vector<Vec2d> holeJoin(hole.begin(), hole.end());
    holeJoin.insert(holeJoin.end(), bridgePoints.begin(), bridgePoints.end());
    const std::vector<Vec2d> outerSplit = SplitRing(outer, holeJoin, tolerance);
    const std::vector<Vec2d> holeSplit = SplitRing(hole, outerSplit, tolerance);

    PointPool pool(tolerance);
    std::vector<std::uint32_t> outerRing;
    outerRing.reserve(outerSplit.size());
    for (const Vec2d& p : outerSplit)
        outerRing.push_back(pool.Intern(p));
    std::vector<std::uint32_t> holeRing;
    holeRing.reserve(holeSplit.size());
    for (const Vec2d& p : holeSplit)
        holeRing.push_back(pool.Intern(p));

    // A ring that interned onto itself has a repeated vertex: degenerate input.
    const auto hasRepeat = [](const std::vector<std::uint32_t>& ring) {
        std::vector<std::uint32_t> sorted = ring;
        std::sort(sorted.begin(), sorted.end());
        return std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end();
    };
    if (hasRepeat(outerRing) || hasRepeat(holeRing))
        return { CarveStatus::InvalidOutline, {} };

    using Arc = std::pair<std::uint32_t, std::uint32_t>;
    const auto ringEdges = [](const std::vector<std::uint32_t>& ring) {
        std::vector<Arc> edges;
        edges.reserve(ring.size());
        for (std::size_t i = 0; i < ring.size(); ++i)
            edges.emplace_back(ring[i], ring[(i + 1) % ring.size()]);
        return edges;
    };
    const std::vector<Arc> outerEdges = ringEdges(outerRing);
    const std::vector<Arc> holeEdges = ringEdges(holeRing);

    // The shared run is the boundary that disappears: it is an outer edge and a
    // hole edge at once, so neither it nor its reverse survives.
    const auto isHoleEdge = [&](Arc e) {
        return std::find(holeEdges.begin(), holeEdges.end(), e) != holeEdges.end();
    };
    const auto isOuterEdge = [&](Arc e) {
        return std::find(outerEdges.begin(), outerEdges.end(), e) != outerEdges.end();
    };

    std::vector<Arc> arcs;
    for (const Arc& e : outerEdges)
        if (!isHoleEdge(e))
            arcs.push_back(e);
    for (const Arc& e : holeEdges)
        if (!isOuterEdge(e))
            arcs.emplace_back(e.second, e.first); // the hole is walked backwards
    if (bridged)
        for (const Bridge& bridge : bridges)
        {
            const std::uint32_t o = pool.Intern(bridge.OuterPoint);
            const std::uint32_t h = pool.Intern(bridge.HolePoint);
            arcs.emplace_back(o, h);
            arcs.emplace_back(h, o); // traversed once by each piece, in opposite directions
        }

    // An outer edge running against a hole edge would have both loops emit the
    // same arc; valid input cannot do it, but a duplicate would desynchronise
    // the traversal's bookkeeping, so they are collapsed here rather than
    // trusted not to appear.
    std::sort(arcs.begin(), arcs.end());
    arcs.erase(std::unique(arcs.begin(), arcs.end()), arcs.end());

    std::vector<std::vector<std::uint32_t>> outgoing;
    std::size_t vertexCount = 0;
    for (const Arc& a : arcs)
        vertexCount = std::max<std::size_t>(vertexCount, std::max(a.first, a.second) + 1u);
    outgoing.resize(vertexCount);
    for (const Arc& a : arcs)
        outgoing[a.first].push_back(a.second);

    // Trace faces by the rotational rule: arriving along (u -> v), leave by the
    // arc after (v -> u) clockwise around v. That is what separates the pieces
    // where they meet at a single vertex, instead of emitting one pinched loop.
    const auto angle = [&](std::uint32_t from, std::uint32_t to) {
        const Vec2d a = pool.At(from);
        const Vec2d b = pool.At(to);
        return std::atan2(b.Y - a.Y, b.X - a.X);
    };
    const auto nextArc = [&](std::uint32_t u, std::uint32_t v) -> std::optional<std::uint32_t> {
        constexpr float kTau = 6.283185307179586f;
        const float base = angle(v, u);
        std::optional<std::uint32_t> best;
        float bestTurn = 0.0f;
        for (std::uint32_t w : outgoing[v])
        {
            float turn = std::fmod(base - angle(v, w) + kTau, kTau);
            if (turn <= 0.0f)
                turn = kTau; // never reverse unless it is the only way out
            if (!best.has_value() || turn < bestTurn)
            {
                best = w;
                bestTurn = turn;
            }
        }
        return best;
    };

    std::vector<Arc> unused = arcs;
    SurroundResult result;
    const std::size_t guard = arcs.size() + 2;
    while (!unused.empty())
    {
        const Arc seed = unused.front();
        std::vector<std::uint32_t> piece{ seed.first };
        std::uint32_t u = seed.first;
        std::uint32_t v = seed.second;
        std::erase(unused, seed);
        std::size_t steps = 0;
        while (v != seed.first)
        {
            piece.push_back(v);
            const std::optional<std::uint32_t> w = nextArc(u, v);
            if (!w.has_value())
                return { CarveStatus::TopologyFailure, {} };
            std::erase(unused, Arc{ v, *w });
            u = v;
            v = *w;
            if (++steps > guard)
                return { CarveStatus::TopologyFailure, {} };
        }
        if (piece.size() < 3)
            continue;
        std::vector<Vec2d> points;
        points.reserve(piece.size());
        for (std::uint32_t index : piece)
            points.push_back(pool.At(index));
        // The arrangement's outer face comes out clockwise; only the material
        // pieces are wanted.
        if (PolygonSignedArea(points) > 0.0f)
            result.Pieces.push_back(std::move(points));
    }

    if (result.Pieces.empty())
        return { CarveStatus::TopologyFailure, {} };
    return result;
}

namespace
{
float Orient(Vec2d a, Vec2d b, Vec2d c)
{
    return (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
}

bool PointOnSegment(Vec2d p, Vec2d a, Vec2d b, float tolerance)
{
    return DistanceToSegment(p, a, b) <= tolerance;
}

// Strictly crossing interiors, which is the only case a shared endpoint or a
// touch is allowed to be.
bool SegmentsProperlyCross(Vec2d a, Vec2d b, Vec2d c, Vec2d d, float tolerance)
{
    if (PointOnSegment(a, c, d, tolerance) || PointOnSegment(b, c, d, tolerance)
        || PointOnSegment(c, a, b, tolerance) || PointOnSegment(d, a, b, tolerance))
        return false;
    return (Orient(a, b, c) > 0.0f) != (Orient(a, b, d) > 0.0f)
        && (Orient(c, d, a) > 0.0f) != (Orient(c, d, b) > 0.0f);
}
}

float DistanceToSegment2D(Vec2d point, Vec2d a, Vec2d b)
{
    return DistanceToSegment(point, a, b);
}

Vec2d SnapPointToPolygon2D(std::span<const Vec2d> rim, Vec2d point, float tolerance)
{
    if (rim.size() < 2)
        return point;
    for (const Vec2d& vertex : rim)
        if (NearlyEqual(vertex, point, tolerance))
            return vertex;

    Vec2d best = point;
    float bestDistance = tolerance;
    for (std::size_t i = 0; i < rim.size(); ++i)
    {
        const Vec2d a = rim[i];
        const Vec2d b = rim[(i + 1) % rim.size()];
        const float distance = DistanceToSegment(point, a, b);
        if (distance > bestDistance)
            continue;
        const float t = std::clamp(ParameterOn(a, b, point), 0.0f, 1.0f);
        bestDistance = distance;
        best = Vec2d{ a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t };
    }
    return best;
}

bool IsSimplePolygon2D(std::span<const Vec2d> polygon, float tolerance)
{
    const std::size_t n = polygon.size();
    if (n < 3)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            if (NearlyEqual(polygon[i], polygon[j], tolerance))
                return false;

    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec2d a = polygon[i];
        const Vec2d b = polygon[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j)
        {
            const Vec2d c = polygon[j];
            const Vec2d d = polygon[(j + 1) % n];
            const bool adjacent = (j == i + 1) || (i == 0 && j + 1 == n);
            if (adjacent)
            {
                // Sharing an endpoint is expected; doubling back along the
                // other edge is not.
                if (PointOnSegment(b, c, d, tolerance) && PointOnSegment(c, a, b, tolerance)
                    && !NearlyEqual(b, c, tolerance))
                    return false;
                if (j == i + 1 && PointOnSegment(d, a, b, tolerance))
                    return false;
                if (i == 0 && j + 1 == n && PointOnSegment(c, a, b, tolerance)
                    && !NearlyEqual(c, b, tolerance) && !NearlyEqual(d, a, tolerance))
                    return false;
                continue;
            }
            if (SegmentsProperlyCross(a, b, c, d, tolerance))
                return false;
            if (PointOnSegment(c, a, b, tolerance) || PointOnSegment(d, a, b, tolerance)
                || PointOnSegment(a, c, d, tolerance) || PointOnSegment(b, c, d, tolerance))
                return false;
        }
    }
    return true;
}

bool PolygonContainsPolygon2D(std::span<const Vec2d> outer, std::span<const Vec2d> inner, float tolerance)
{
    if (outer.size() < 3 || inner.size() < 3)
        return false;

    for (std::size_t i = 0; i < inner.size(); ++i)
    {
        const Vec2d a = inner[i];
        const Vec2d b = inner[(i + 1) % inner.size()];
        if (ClassifyPointInPolygon2D(outer, a, tolerance) == PointPolygonRelation::Outside)
            return false;

        // Every parameter along (a, b) where it meets the outer boundary, so
        // the edge can be classified piece by piece instead of at a sample the
        // excursion happens to miss.
        std::vector<float> cuts = { 0.0f, 1.0f };
        for (std::size_t j = 0; j < outer.size(); ++j)
        {
            const Vec2d c = outer[j];
            const Vec2d d = outer[(j + 1) % outer.size()];
            if (PointOnSegment(c, a, b, tolerance))
                cuts.push_back(std::clamp(ParameterOn(a, b, c), 0.0f, 1.0f));
            if (PointOnSegment(d, a, b, tolerance))
                cuts.push_back(std::clamp(ParameterOn(a, b, d), 0.0f, 1.0f));
            if (SegmentsProperlyCross(a, b, c, d, tolerance))
            {
                const float denominator = Orient(c, d, b) - Orient(c, d, a);
                if (std::abs(denominator) > 0.0f)
                    cuts.push_back(std::clamp(Orient(c, d, a) / -denominator, 0.0f, 1.0f));
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t k = 0; k + 1 < cuts.size(); ++k)
        {
            const float span = cuts[k + 1] - cuts[k];
            if (span <= 0.0f)
                continue;
            const float t = cuts[k] + span * 0.5f;
            const Vec2d point{ a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t };
            if (ClassifyPointInPolygon2D(outer, point, tolerance) == PointPolygonRelation::Outside)
                return false;
        }
    }
    return true;
}

std::vector<Vec2d> ClipPolygonToConvex2D(std::span<const Vec2d> polygon, std::span<const Vec2d> convex,
                                         float tolerance)
{
    if (convex.size() < 3)
        return {};
    std::vector<Vec2d> current(polygon.begin(), polygon.end());
    for (std::size_t e = 0; e < convex.size(); ++e)
    {
        if (current.size() < 3)
            return {};
        // The half-plane to the left of the edge, since the region is CCW.
        const Vec2d a = convex[e];
        const Vec2d b = convex[(e + 1) % convex.size()];
        const Vec2d edge{ b.X - a.X, b.Y - a.Y };
        const float length = std::sqrt(edge.X * edge.X + edge.Y * edge.Y);
        if (length <= tolerance)
            continue;
        const auto side = [&](Vec2d p) { return (edge.X * (p.Y - a.Y) - edge.Y * (p.X - a.X)) / length; };
        const auto inside = [&](Vec2d p) { return side(p) >= -tolerance; };
        const auto onto = [&](Vec2d p, Vec2d q) {
            const float sp = side(p);
            const float sq = side(q);
            const float t = sp / (sp - sq);
            return Vec2d{ p.X + (q.X - p.X) * t, p.Y + (q.Y - p.Y) * t };
        };
        std::vector<Vec2d> next;
        next.reserve(current.size() + 2);
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            const Vec2d p = current[i];
            const Vec2d q = current[(i + 1) % current.size()];
            const bool pIn = inside(p);
            const bool qIn = inside(q);
            if (pIn)
                next.push_back(p);
            if (pIn != qIn)
                next.push_back(onto(p, q));
        }
        current = std::move(next);
    }

    // Merge the points a clip lands on top of one another (a vertex on the
    // line, a corner cut twice), then drop anything without area.
    std::vector<Vec2d> merged;
    for (const Vec2d& p : current)
        if (merged.empty() || DistanceToSegment2D(p, merged.back(), merged.back()) > tolerance)
            merged.push_back(p);
    while (merged.size() > 1 && DistanceToSegment2D(merged.back(), merged.front(), merged.front()) <= tolerance)
        merged.pop_back();
    if (merged.size() < 3 || std::abs(PolygonSignedArea(merged)) <= tolerance * tolerance)
        return {};
    return merged;
}

std::vector<Vec2d> ClipPolygonToRect2D(std::span<const Vec2d> polygon, Vec2d min, Vec2d max,
                                       float tolerance)
{
    const Vec2d rect[4] = { min, { max.X, min.Y }, max, { min.X, max.Y } };
    std::vector<Vec2d> clipped = ClipPolygonToConvex2D(polygon, rect, tolerance);
    // Points the clip put on a rectangle side land exactly on it, not a rounded
    // step short of it, so a later snap onto that side has nothing to move.
    for (Vec2d& p : clipped)
    {
        if (std::abs(p.X - min.X) <= tolerance) p.X = min.X;
        if (std::abs(p.X - max.X) <= tolerance) p.X = max.X;
        if (std::abs(p.Y - min.Y) <= tolerance) p.Y = min.Y;
        if (std::abs(p.Y - max.Y) <= tolerance) p.Y = max.Y;
    }
    return clipped;
}

bool IsConvexPolygon2D(std::span<const Vec2d> polygon, float tolerance)
{
    const std::size_t n = polygon.size();
    if (n < 3)
        return false;
    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec2d a = polygon[i];
        const Vec2d b = polygon[(i + 1) % n];
        const Vec2d c = polygon[(i + 2) % n];
        const float ab = DistanceToSegment(a, b, b);
        if (ab <= tolerance)
            continue;
        // The turn at b, as the distance of c from line ab: a right turn deeper
        // than the tolerance is a reflex corner.
        if (Orient(a, b, c) / ab < -tolerance)
            return false;
    }
    return true;
}

namespace
{
// Points of `polygon` that are inside it when the polygon is simple: its edge
// midpoints (on the boundary, so they tell only against the other polygon) and
// the midpoints of the chords across its convex corners. Vertices alone cannot
// tell two coincident polygons apart from two that merely touch.
bool AnySampleInside(std::span<const Vec2d> polygon, std::span<const Vec2d> other, float tolerance)
{
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec2d a = polygon[i];
        const Vec2d b = polygon[(i + 1) % n];
        const Vec2d c = polygon[(i + 2) % n];
        const Vec2d edgeMid{ (a.X + b.X) * 0.5f, (a.Y + b.Y) * 0.5f };
        if (ClassifyPointInPolygon2D(other, edgeMid, tolerance) == PointPolygonRelation::Inside)
            return true;
        const Vec2d chordMid{ (a.X + c.X) * 0.5f, (a.Y + c.Y) * 0.5f };
        if (Orient(a, b, c) > 0.0f
            && ClassifyPointInPolygon2D(polygon, chordMid, tolerance) == PointPolygonRelation::Inside
            && ClassifyPointInPolygon2D(other, chordMid, tolerance) == PointPolygonRelation::Inside)
            return true;
    }
    return false;
}
}

bool PolygonsOverlap2D(std::span<const Vec2d> a, std::span<const Vec2d> b, float tolerance)
{
    for (const Vec2d& p : a)
        if (ClassifyPointInPolygon2D(b, p, tolerance) == PointPolygonRelation::Inside)
            return true;
    for (const Vec2d& p : b)
        if (ClassifyPointInPolygon2D(a, p, tolerance) == PointPolygonRelation::Inside)
            return true;
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            if (SegmentsProperlyCross(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()], tolerance))
                return true;
    return AnySampleInside(a, b, tolerance) || AnySampleInside(b, a, tolerance);
}

std::optional<Vec2d> SegmentCrossing2D(Vec2d a, Vec2d b, Vec2d c, Vec2d d, float tolerance)
{
    if (!SegmentsProperlyCross(a, b, c, d, tolerance))
        return std::nullopt;
    const float denominator = (b.X - a.X) * (d.Y - c.Y) - (b.Y - a.Y) * (d.X - c.X);
    if (std::abs(denominator) <= 1e-12f)
        return std::nullopt;
    const float t = ((c.X - a.X) * (d.Y - c.Y) - (c.Y - a.Y) * (d.X - c.X)) / denominator;
    return Vec2d{ a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t };
}
