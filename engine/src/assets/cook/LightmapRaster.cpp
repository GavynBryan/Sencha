#include <assets/cook/LightmapRaster.h>

#include <assets/cook/BakeBvh.h>

#include <algorithm>
#include <cmath>

namespace
{
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

    struct LuxelSample
    {
        Vec3d Position;
        Vec3d Normal;
        // 0 = uncovered, 1 = edge-clamped sample, 2 = interior sample.
        std::uint8_t Coverage = 0;
    };
} // namespace

void BakeChartLuxels(std::span<const LightmapRasterTriangle> triangles,
                     const LightmapChartRect& rect,
                     std::span<const BakeDirectLight> lights,
                     const BakeBvh& occluders,
                     const DirectLightBakeParams& params,
                     std::uint32_t atlasWidth,
                     std::span<std::uint32_t> atlasPixels)
{
    if (rect.Width <= 2 * kLightmapGutter || rect.Height <= 2 * kLightmapGutter)
        return;
    const std::uint32_t gridW = rect.Width - 2 * kLightmapGutter;
    const std::uint32_t gridH = rect.Height - 2 * kLightmapGutter;

    std::vector<LuxelSample> samples(static_cast<std::size_t>(gridW) * gridH);

    // Half the luxel diagonal: a grid point this close to a triangle still
    // samples it (clamped onto the surface), so sliver triangles stay lit.
    constexpr float kEdgeReach = 0.7071f;

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

        const float minX = std::min(a.X, std::min(b.X, c.X)) - kEdgeReach;
        const float maxX = std::max(a.X, std::max(b.X, c.X)) + kEdgeReach;
        const float minY = std::min(a.Y, std::min(b.Y, c.Y)) - kEdgeReach;
        const float maxY = std::max(a.Y, std::max(b.Y, c.Y)) + kEdgeReach;
        const std::uint32_t x0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor(minX)));
        const std::uint32_t y0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor(minY)));
        const std::uint32_t x1 = static_cast<std::uint32_t>(
            std::clamp(std::ceil(maxX), 0.0f, static_cast<float>(gridW - 1)));
        const std::uint32_t y1 = static_cast<std::uint32_t>(
            std::clamp(std::ceil(maxY), 0.0f, static_cast<float>(gridH - 1)));

        for (std::uint32_t y = y0; y <= y1 && y < gridH; ++y)
            for (std::uint32_t x = x0; x <= x1 && x < gridW; ++x)
            {
                LuxelSample& sample = samples[static_cast<std::size_t>(y) * gridW + x];
                if (sample.Coverage == 2)
                    continue; // interior already claimed this luxel

                const Vec2d p{ static_cast<float>(x), static_cast<float>(y) };
                const Vec2d v2{ p.X - a.X, p.Y - a.Y };
                float wb = Cross2(v2, v1) / den;
                float wc = Cross2(v0, v2) / den;
                float wa = 1.0f - wb - wc;

                std::uint8_t coverage = 0;
                if (wa >= -1e-4f && wb >= -1e-4f && wc >= -1e-4f)
                {
                    coverage = 2;
                }
                else
                {
                    // Off-triangle: clamp to the closest boundary point and
                    // accept it while within the edge reach.
                    float bestDistSq = kEdgeReach * kEdgeReach + 1.0f;
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
                    if (bestDistSq <= kEdgeReach * kEdgeReach)
                        coverage = 1;
                }

                if (coverage == 0 || coverage <= sample.Coverage)
                    continue; // first writer wins within a class

                sample.Coverage = coverage;
                sample.Position = tri.Position[0] * wa + tri.Position[1] * wb
                    + tri.Position[2] * wc;
                sample.Normal = (tri.Normal[0] * wa + tri.Normal[1] * wb
                    + tri.Normal[2] * wc);
            }
    }

    // Padded-rect radiance buffer: covered grid points bake, then dilation
    // fills the gutter (and any uncovered interior gaps) from covered
    // neighbors so bilinear filtering reads plausible values everywhere.
    struct Texel
    {
        Vec3d Radiance{ 0.0f, 0.0f, 0.0f };
        bool Covered = false;
    };
    std::vector<Texel> texels(static_cast<std::size_t>(rect.Width) * rect.Height);

    for (std::uint32_t y = 0; y < gridH; ++y)
        for (std::uint32_t x = 0; x < gridW; ++x)
        {
            const LuxelSample& sample = samples[static_cast<std::size_t>(y) * gridW + x];
            if (sample.Coverage == 0)
                continue;
            const Vec3d normal = sample.Normal.SqrMagnitude() > 1e-12f
                ? sample.Normal.Normalized()
                : Vec3d{ 0.0f, 1.0f, 0.0f };
            // Buried samples (a chart running underneath an overlapping brush
            // sees a backface first along its own normal) stay uncovered:
            // dilation continues the neighboring lighting across them instead
            // of baking black that bilinear filtering would bleed out past
            // the overlapping geometry's base.
            const Vec3d probeOrigin = sample.Position + normal * params.NormalOffset;
            if (occluders.FirstHitIsBackface(probeOrigin, normal, 1e6))
                continue;
            Texel& texel = texels[static_cast<std::size_t>(y + kLightmapGutter) * rect.Width
                + x + kLightmapGutter];
            texel.Radiance = EvaluateBakedDirectRadiance(
                sample.Position, normal, lights, occluders, params);
            texel.Covered = true;
        }

    // Two ping-pong dilation passes (read previous, write next) keep the fill
    // order-independent; two passes fill the 2-texel gutter exactly.
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<Texel> next = texels;
        for (std::uint32_t y = 0; y < rect.Height; ++y)
            for (std::uint32_t x = 0; x < rect.Width; ++x)
            {
                Texel& out = next[static_cast<std::size_t>(y) * rect.Width + x];
                if (out.Covered)
                    continue;
                Vec3d sum{ 0.0f, 0.0f, 0.0f };
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        const int nx = static_cast<int>(x) + dx;
                        const int ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(rect.Width)
                            || ny >= static_cast<int>(rect.Height))
                            continue;
                        const Texel& neighbor =
                            texels[static_cast<std::size_t>(ny) * rect.Width + nx];
                        if (!neighbor.Covered)
                            continue;
                        sum += neighbor.Radiance;
                        ++count;
                    }
                if (count > 0)
                {
                    out.Radiance = sum / static_cast<float>(count);
                    out.Covered = true;
                }
            }
        texels = std::move(next);
    }

    for (std::uint32_t y = 0; y < rect.Height; ++y)
        for (std::uint32_t x = 0; x < rect.Width; ++x)
        {
            const Texel& texel = texels[static_cast<std::size_t>(y) * rect.Width + x];
            atlasPixels[static_cast<std::size_t>(rect.Y + y) * atlasWidth + rect.X + x] =
                EncodeBakedDirectRgbm(texel.Radiance);
        }
}
