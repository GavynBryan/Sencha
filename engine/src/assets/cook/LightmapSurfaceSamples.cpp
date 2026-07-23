#include <assets/cook/LightmapSurfaceSamples.h>

#include <assets/cook/BakeBvh.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
    constexpr float kEdgeReach = 0.7071f;
    constexpr float kSampleInset = 0.0025f;

    float Cross2(const Vec2d& a, const Vec2d& b)
    {
        return a.X * b.Y - a.Y * b.X;
    }

    // Closest point to p on segment [a, b], as the segment parameter t.
    float SegmentClosestT(const Vec2d& a, const Vec2d& b, const Vec2d& p)
    {
        const Vec2d ab{ b.X - a.X, b.Y - a.Y };
        const float lenSq = ab.X * ab.X + ab.Y * ab.Y;
        if (lenSq <= 1e-12f)
            return 0.0f;
        const float t = ((p.X - a.X) * ab.X + (p.Y - a.Y) * ab.Y) / lenSq;
        return std::clamp(t, 0.0f, 1.0f);
    }

    struct RasterTriangle
    {
        const LightmapRasterTriangle* Tri = nullptr;
        Vec2d Edge0;
        Vec2d Edge1;
        float Den = 0.0f;
    };

    struct ResolvedSample
    {
        const RasterTriangle* Tri = nullptr;
        float Weight[3] = { 0.0f, 0.0f, 0.0f };
        Vec2d SurfacePoint;
        bool EdgeClamped = false;
    };

    // Barycentric weights of p against the triangle (a, b, c), clamped onto
    // the triangle when p lies outside it (closest boundary point). Returns
    // the squared grid-space distance from p to the sampled point (0 inside).
    float ClampedBarycentric(const Vec2d& a, const Vec2d& b, const Vec2d& c,
                             float den, const Vec2d& p,
                             float& wa, float& wb, float& wc)
    {
        const Vec2d v0{ b.X - a.X, b.Y - a.Y };
        const Vec2d v1{ c.X - a.X, c.Y - a.Y };
        const Vec2d v2{ p.X - a.X, p.Y - a.Y };
        wb = Cross2(v2, v1) / den;
        wc = Cross2(v0, v2) / den;
        wa = 1.0f - wb - wc;
        if (wa >= -1e-4f && wb >= -1e-4f && wc >= -1e-4f)
            return 0.0f;

        float bestDistSq = std::numeric_limits<float>::max();
        const Vec2d corners[3] = { a, b, c };
        for (int e = 0; e < 3; ++e)
        {
            const Vec2d& s0 = corners[e];
            const Vec2d& s1 = corners[(e + 1) % 3];
            const float t = SegmentClosestT(s0, s1, p);
            const Vec2d q{ s0.X + (s1.X - s0.X) * t, s0.Y + (s1.Y - s0.Y) * t };
            const float dx = p.X - q.X;
            const float dy = p.Y - q.Y;
            const float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                const Vec2d vq{ q.X - a.X, q.Y - a.Y };
                wb = Cross2(vq, v1) / den;
                wc = Cross2(v0, vq) / den;
                wa = 1.0f - wb - wc;
            }
        }
        return bestDistSq;
    }

    bool TriangleLess(const RasterTriangle& lhs, const RasterTriangle& rhs)
    {
        // Candidate order must not inherit mesh triangle order: points on a
        // shared edge can be contained by both triangles. Compare all authored
        // data so even coincident chart-space triangles have a stable order.
        for (int vertex = 0; vertex < 3; ++vertex)
        {
            for (int axis = 0; axis < 2; ++axis)
            {
                const float a = lhs.Tri->Uv[vertex][axis];
                const float b = rhs.Tri->Uv[vertex][axis];
                if (a != b)
                    return a < b;
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                const float a = lhs.Tri->Position[vertex][axis];
                const float b = rhs.Tri->Position[vertex][axis];
                if (a != b)
                    return a < b;
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                const float a = lhs.Tri->Normal[vertex][axis];
                const float b = rhs.Tri->Normal[vertex][axis];
                if (a != b)
                    return a < b;
            }
        }
        return false;
    }

    ResolvedSample ResolveSample(const Vec2d& p,
                                 const std::vector<std::uint32_t>& candidates,
                                 const std::vector<RasterTriangle>& rasterTriangles)
    {
        ResolvedSample nearest;
        float nearestDistSq = kEdgeReach * kEdgeReach;
        constexpr float kDistanceTieEps = 1e-7f;
        for (const std::uint32_t index : candidates)
        {
            const RasterTriangle& rasterTri = rasterTriangles[index];
            const LightmapRasterTriangle& tri = *rasterTri.Tri;
            float wa = 0.0f, wb = 0.0f, wc = 0.0f;
            const float distSq = ClampedBarycentric(
                tri.Uv[0], tri.Uv[1], tri.Uv[2], rasterTri.Den, p,
                wa, wb, wc);
            const Vec2d surfacePoint = tri.Uv[0] * wa
                + tri.Uv[1] * wb + tri.Uv[2] * wc;

            // rasterTriangles (and therefore every candidate list) has a
            // geometry-derived order. The first containing triangle is the
            // deterministic winner on a shared edge.
            if (distSq <= 0.0f)
                return ResolvedSample{
                    &rasterTri, { wa, wb, wc }, surfacePoint, false };

            // A chart corner can be equally close to two real boundary edges.
            // The triangles reaching that corner change with a quad diagonal,
            // so resolve the tie by the geometric closest point in chart space,
            // not candidate/triangle order.
            const bool closer = nearest.Tri == nullptr
                || distSq < nearestDistSq - kDistanceTieEps;
            const bool tied = nearest.Tri != nullptr
                && std::abs(distSq - nearestDistSq) <= kDistanceTieEps;
            const bool pointLess = tied
                && (surfacePoint.X < nearest.SurfacePoint.X
                    || (surfacePoint.X == nearest.SurfacePoint.X
                        && surfacePoint.Y < nearest.SurfacePoint.Y));
            if (distSq <= kEdgeReach * kEdgeReach && (closer || pointLess))
            {
                nearest = ResolvedSample{
                    &rasterTri, { wa, wb, wc }, surfacePoint, true };
                nearestDistSq = distSq;
            }
        }
        return nearest;
    }

    Vec3d InterpolatePosition(const LightmapRasterTriangle& tri,
                              const float weights[3])
    {
        return tri.Position[0] * weights[0] + tri.Position[1] * weights[1]
            + tri.Position[2] * weights[2];
    }

    Vec3d InterpolateNormal(const LightmapRasterTriangle& tri,
                            const float weights[3])
    {
        return tri.Normal[0] * weights[0] + tri.Normal[1] * weights[1]
            + tri.Normal[2] * weights[2];
    }

    void InsetEdgeSample(const Vec2d& samplePoint,
                         const RasterTriangle& rasterTri,
                         const float weights[3], Vec3d& position)
    {
        const LightmapRasterTriangle& tri = *rasterTri.Tri;
        const Vec2d surfacePoint = tri.Uv[0] * weights[0]
            + tri.Uv[1] * weights[1] + tri.Uv[2] * weights[2];
        const Vec2d towardSurface = surfacePoint - samplePoint;

        // Map the chart-space direction from the off-surface sample to its
        // closest boundary point into world space, then continue a hair past
        // the boundary. Unlike a centroid direction, this depends only on the
        // actual chart edge and cannot expose an internal triangulation.
        const float db = Cross2(towardSurface, rasterTri.Edge1) / rasterTri.Den;
        const float dc = Cross2(rasterTri.Edge0, towardSurface) / rasterTri.Den;
        Vec3d inward = (tri.Position[1] - tri.Position[0]) * db
            + (tri.Position[2] - tri.Position[0]) * dc;
        float distance = inward.Magnitude();
        if (distance <= 1e-12f)
        {
            inward = (tri.Position[0] + tri.Position[1] + tri.Position[2]) / 3.0f
                - position;
            distance = inward.Magnitude();
        }
        if (distance > kSampleInset)
            position += inward * (kSampleInset / distance);
    }

    // 2x2 supersampling per luxel: a luxel is one texel of an all-or-nothing
    // visibility function, and a lit patch smaller than a texel otherwise bakes
    // as an isolated full-bright pinhole in the middle of shadow. Averaging four
    // in-luxel samples turns partial coverage into partial brightness.
    constexpr Vec2d kSubsamples[4] = {
        { -0.25f, -0.25f }, { 0.25f, -0.25f }, { -0.25f, 0.25f }, { 0.25f, 0.25f }
    };
} // namespace

LightmapSurfaceSamples ResolveLightmapSurfaceSamples(
    std::span<const LightmapRasterTriangle> triangles,
    const LightmapChartRect& rect, const BakeBvh& occluders, float normalOffset)
{
    LightmapSurfaceSamples out;
    if (rect.Width <= 2 * kLightmapGutter || rect.Height <= 2 * kLightmapGutter)
        return out;
    const std::uint32_t gridW = rect.Width - 2 * kLightmapGutter;
    const std::uint32_t gridH = rect.Height - 2 * kLightmapGutter;
    out.GridWidth = gridW;
    out.GridHeight = gridH;

    std::vector<RasterTriangle> rasterTriangles;
    rasterTriangles.reserve(triangles.size());
    for (const LightmapRasterTriangle& tri : triangles)
    {
        const Vec2d a = tri.Uv[0];
        const Vec2d b = tri.Uv[1];
        const Vec2d c = tri.Uv[2];
        const Vec2d v0{ b.X - a.X, b.Y - a.Y };
        const Vec2d v1{ c.X - a.X, c.Y - a.Y };
        const float den = Cross2(v0, v1);
        if (std::abs(den) <= 1e-9f)
            continue; // degenerate in chart space; neighbors + dilation cover it
        rasterTriangles.push_back(RasterTriangle{ &tri, v0, v1, den });
    }
    std::sort(rasterTriangles.begin(), rasterTriangles.end(), TriangleLess);

    // Each luxel keeps every triangle that any of its four offsets could
    // resolve. The extra quarter-texel accounts for the subsample displacement
    // before applying the existing half-diagonal edge reach.
    std::vector<std::vector<std::uint32_t>> candidates(
        static_cast<std::size_t>(gridW) * gridH);
    constexpr float kCandidateReach = kEdgeReach + 0.25f;
    for (std::uint32_t index = 0; index < rasterTriangles.size(); ++index)
    {
        const LightmapRasterTriangle& tri = *rasterTriangles[index].Tri;
        const Vec2d& a = tri.Uv[0];
        const Vec2d& b = tri.Uv[1];
        const Vec2d& c = tri.Uv[2];

        const float minX = std::min(a.X, std::min(b.X, c.X)) - kCandidateReach;
        const float maxX = std::max(a.X, std::max(b.X, c.X)) + kCandidateReach;
        const float minY = std::min(a.Y, std::min(b.Y, c.Y)) - kCandidateReach;
        const float maxY = std::max(a.Y, std::max(b.Y, c.Y)) + kCandidateReach;
        if (maxX < 0.0f || maxY < 0.0f
            || minX > static_cast<float>(gridW - 1)
            || minY > static_cast<float>(gridH - 1))
            continue;
        const std::uint32_t x0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor(minX)));
        const std::uint32_t y0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor(minY)));
        const std::uint32_t x1 = static_cast<std::uint32_t>(
            std::clamp(std::ceil(maxX), 0.0f, static_cast<float>(gridW - 1)));
        const std::uint32_t y1 = static_cast<std::uint32_t>(
            std::clamp(std::ceil(maxY), 0.0f, static_cast<float>(gridH - 1)));

        for (std::uint32_t y = y0; y <= y1 && y < gridH; ++y)
            for (std::uint32_t x = x0; x <= x1 && x < gridW; ++x)
                candidates[static_cast<std::size_t>(y) * gridW + x].push_back(index);
    }

    out.Offsets.assign(static_cast<std::size_t>(gridW) * gridH + 1, 0u);
    for (std::uint32_t y = 0; y < gridH; ++y)
        for (std::uint32_t x = 0; x < gridW; ++x)
        {
            const std::size_t gi = static_cast<std::size_t>(y) * gridW + x;
            const std::vector<std::uint32_t>& luxelCandidates = candidates[gi];
            for (const Vec2d& offset : kSubsamples)
            {
                if (luxelCandidates.empty())
                    break;
                const Vec2d p{ static_cast<float>(x) + offset.X,
                               static_cast<float>(y) + offset.Y };
                const ResolvedSample resolved =
                    ResolveSample(p, luxelCandidates, rasterTriangles);
                if (resolved.Tri == nullptr)
                    continue;
                const LightmapRasterTriangle& tri = *resolved.Tri->Tri;
                Vec3d position = InterpolatePosition(tri, resolved.Weight);
                // Only off-surface samples need the chart-boundary safeguard.
                if (resolved.EdgeClamped)
                    InsetEdgeSample(p, *resolved.Tri, resolved.Weight, position);
                const Vec3d rawNormal = InterpolateNormal(tri, resolved.Weight);
                const Vec3d normal = rawNormal.SqrMagnitude() > 1e-12f
                    ? rawNormal.Normalized()
                    : Vec3d{ 0.0f, 1.0f, 0.0f };

                // Buried subsamples (a chart running underneath overlapping
                // geometry) contribute nothing. Buried means ENCLOSED: a
                // backface first along the normal AND along its reverse. The
                // reverse probe skips twice the lift so the sample's own surface
                // and flush partners are not the "behind" hit.
                const Vec3d probeOrigin = position + normal * normalOffset;
                if (occluders.FirstHitIsBackface(probeOrigin, normal, 1e6)
                    && occluders.FirstHitIsBackface(probeOrigin, normal * -1.0f, 1e6,
                                                    2.0 * normalOffset))
                    continue;

                out.Samples.push_back(LightmapSurfaceSample{ position, normal });
            }
            out.Offsets[gi + 1] = static_cast<std::uint32_t>(out.Samples.size());
        }
    return out;
}
