#include <assets/cook/ProbeBake.h>

#include <assets/probes/ProbeVolumeFormat.h>
#include <jobs/JobSystem.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    // Real SH basis constants: Y00 and the shared Y1* factor.
    constexpr float kShY0 = 0.2820948f;   // 1 / (2 sqrt(pi))
    constexpr float kShY1 = 0.4886025f;   // sqrt(3 / (4 pi))
    // Lambert cosine-lobe convolution, folded with the 1/pi diffuse factor:
    // A0 = pi, A1 = 2pi/3, so E(n)/pi uses kShY0 * 1 and kShY1 * 2/3.
    constexpr float kShConvolve0 = kShY0;
    constexpr float kShConvolve1 = kShY1 * (2.0f / 3.0f);

    void AccumulateSample(ProbeShL1& sh, const Vec3d& direction,
                          const Vec3d& radiance, float weight)
    {
        const Vec4 basis(kShY1 * direction.X * weight,
                         kShY1 * direction.Y * weight,
                         kShY1 * direction.Z * weight,
                         kShY0 * weight);
        sh.R += basis * radiance.X;
        sh.G += basis * radiance.Y;
        sh.B += basis * radiance.Z;
    }

    Vec3d HemisphericMiss(const Vec3d& direction, const ProbeBakeParams& params)
    {
        const float hemi = 0.5f + 0.5f * direction.Y;
        return params.GroundColor + (params.SkyColor - params.GroundColor) * hemi;
    }

    ProbeShL1 BakeOneProbe(const Vec3d& position, const BakeBvh& occluders,
                           std::span<const BakeDirectLight> lights,
                           std::span<const Vec3d> rayTable,
                           const ProbeBakeParams& params)
    {
        // Monte Carlo projection over the uniform sphere table:
        // c = (4 pi / N) * sum L(d) * Y(d).
        const float weight =
            4.0f * std::numbers::pi_v<float> / static_cast<float>(rayTable.size());

        ProbeShL1 sh{};
        for (const Vec3d& direction : rayTable)
        {
            BakeBvhHit hit;
            if (!occluders.FirstHit(position, direction, params.MaxRayDistance, hit))
            {
                AccumulateSample(sh, direction, HemisphericMiss(direction, params), weight);
                continue;
            }
            if (hit.Backface)
                continue;  // inside-facing surface contributes black

            // One bounce: the surface's baked direct lighting, reflected with
            // a constant albedo. The hit normal already faces the probe.
            const Vec3d direct = EvaluateBakedDirectRadiance(
                hit.Position, hit.Normal, lights, occluders, params.Shading);
            AccumulateSample(sh, direction, direct * params.BounceAlbedo, weight);
        }
        return sh;
    }

    bool ProbeIsBuried(const Vec3d& position, const BakeBvh& occluders,
                       std::span<const Vec3d> rayTable,
                       const ProbeBakeParams& params)
    {
        std::uint32_t backfaceHits = 0;
        for (const Vec3d& direction : rayTable)
        {
            BakeBvhHit hit;
            if (occluders.FirstHit(position, direction, params.ClassifyRayDistance, hit)
                && hit.Backface)
                ++backfaceHits;
        }
        return static_cast<float>(backfaceHits)
             > params.ClassifyBackfaceRatio * static_cast<float>(rayTable.size());
    }
}

std::vector<Vec3d> BuildProbeRayTable(std::uint32_t count)
{
    // Spherical Fibonacci spiral: near-uniform over the sphere, closed-form,
    // no randomness. Computed in double so the table is platform-stable.
    const double goldenAngle =
        std::numbers::pi * (3.0 - std::sqrt(5.0));
    std::vector<Vec3d> table;
    table.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const double z = 1.0 - 2.0 * (static_cast<double>(i) + 0.5)
                                   / static_cast<double>(count);
        const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double angle = goldenAngle * static_cast<double>(i);
        table.push_back(Vec3d(static_cast<float>(radius * std::cos(angle)),
                              static_cast<float>(z),
                              static_cast<float>(radius * std::sin(angle))));
    }
    return table;
}

ProbeShL1 ProjectHemisphericAmbientSh(const Vec3d& sky, const Vec3d& ground)
{
    // L(d) = mean + halfDiff * d.y projects analytically: the constant term
    // integrates against Y00 over 4 pi, the linear term against Y1y over
    // 4 pi / 3; the x/z coefficients vanish by symmetry.
    constexpr float kFourPi = 4.0f * std::numbers::pi_v<float>;
    const Vec3d mean = (sky + ground) * 0.5f;
    const Vec3d halfDiff = (sky - ground) * 0.5f;
    const float c0 = kShY0 * kFourPi;
    const float c1y = kShY1 * kFourPi / 3.0f;

    ProbeShL1 sh{};
    sh.R = Vec4(0.0f, c1y * halfDiff.X, 0.0f, c0 * mean.X);
    sh.G = Vec4(0.0f, c1y * halfDiff.Y, 0.0f, c0 * mean.Y);
    sh.B = Vec4(0.0f, c1y * halfDiff.Z, 0.0f, c0 * mean.Z);
    return sh;
}

Vec3d EvaluateProbeShL1(const ProbeShL1& sh, const Vec3d& normal)
{
    const Vec4 eval(kShConvolve1 * normal.X,
                    kShConvolve1 * normal.Y,
                    kShConvolve1 * normal.Z,
                    kShConvolve0);
    return Vec3d(std::max(0.0f, sh.R.Dot(eval)),
                 std::max(0.0f, sh.G.Dot(eval)),
                 std::max(0.0f, sh.B.Dot(eval)));
}

ProbeVolumeBakeResult BakeProbeVolume(const GridTransform3d& grid,
                                      const BakeBvh& occluders,
                                      std::span<const BakeDirectLight> lights,
                                      std::span<const Vec3d> rayTable,
                                      const ProbeBakeParams& params,
                                      JobSystem* jobs)
{
    const std::uint32_t count = grid.PointCount();
    ProbeVolumeBakeResult result;
    result.Sh.resize(count);
    result.Valid.assign(count, 0);

    // Each layer job touches only its own slots, and every probe accumulates
    // in fixed table order, so scheduling cannot change a single bit.
    const auto bakeLayer = [&](std::uint32_t z)
    {
        for (std::uint32_t y = 0; y < grid.DimsY; ++y)
            for (std::uint32_t x = 0; x < grid.DimsX; ++x)
            {
                const std::uint32_t index = grid.FlatIndex(x, y, z);
                const Vec3d position = grid.PointPosition(x, y, z);
                if (ProbeIsBuried(position, occluders, rayTable, params))
                    continue;
                result.Sh[index] =
                    BakeOneProbe(position, occluders, lights, rayTable, params);
                result.Valid[index] = 1;
            }
    };

    if (jobs != nullptr && jobs->WorkerCount() > 0)
        jobs->ParallelFor(grid.DimsZ, [&](std::uint32_t z) { bakeLayer(z); });
    else
        for (std::uint32_t z = 0; z < grid.DimsZ; ++z)
            bakeLayer(z);

    for (std::uint32_t i = 0; i < count; ++i)
        if (result.Valid[i] == 0)
            ++result.InvalidCount;

    // Dilation: fill invalid probes from face-adjacent filled neighbors, one
    // synchronized round at a time (reads the previous round's fill set, so
    // fill order within a round cannot matter).
    std::vector<std::uint8_t> filled = result.Valid;
    bool progressed = true;
    while (progressed)
    {
        progressed = false;
        std::vector<std::uint8_t> nextFilled = filled;
        std::vector<ProbeShL1> nextSh = result.Sh;
        for (std::uint32_t z = 0; z < grid.DimsZ; ++z)
            for (std::uint32_t y = 0; y < grid.DimsY; ++y)
                for (std::uint32_t x = 0; x < grid.DimsX; ++x)
                {
                    const std::uint32_t index = grid.FlatIndex(x, y, z);
                    if (filled[index] != 0)
                        continue;

                    ProbeShL1 sum{};
                    std::uint32_t neighbors = 0;
                    const auto accumulate = [&](std::uint32_t nx, std::uint32_t ny,
                                                std::uint32_t nz)
                    {
                        const std::uint32_t n = grid.FlatIndex(nx, ny, nz);
                        if (filled[n] == 0)
                            return;
                        sum.R += result.Sh[n].R;
                        sum.G += result.Sh[n].G;
                        sum.B += result.Sh[n].B;
                        ++neighbors;
                    };
                    if (x > 0) accumulate(x - 1, y, z);
                    if (x + 1 < grid.DimsX) accumulate(x + 1, y, z);
                    if (y > 0) accumulate(x, y - 1, z);
                    if (y + 1 < grid.DimsY) accumulate(x, y + 1, z);
                    if (z > 0) accumulate(x, y, z - 1);
                    if (z + 1 < grid.DimsZ) accumulate(x, y, z + 1);
                    if (neighbors == 0)
                        continue;

                    const float inv = 1.0f / static_cast<float>(neighbors);
                    nextSh[index].R = sum.R * inv;
                    nextSh[index].G = sum.G * inv;
                    nextSh[index].B = sum.B * inv;
                    nextFilled[index] = 1;
                    progressed = true;
                }
        filled = std::move(nextFilled);
        result.Sh = std::move(nextSh);
    }

    // A fully enclosed region no dilation reached holds the hemispheric
    // ambient, so sampling there degrades to today's look instead of black.
    const ProbeShL1 ambient =
        ProjectHemisphericAmbientSh(params.SkyColor, params.GroundColor);
    for (std::uint32_t i = 0; i < count; ++i)
        if (filled[i] == 0)
        {
            result.Sh[i] = ambient;
            ++result.UnfilledCount;
        }

    return result;
}

std::vector<std::uint16_t> PackProbeShHalf(std::span<const ProbeShL1> sh)
{
    std::vector<std::uint16_t> halves;
    halves.reserve(sh.size() * kProbeShChannels * kProbeShCoefficients);
    const auto plane = [&](auto&& channel)
    {
        for (const ProbeShL1& probe : sh)
        {
            const Vec4& c = channel(probe);
            halves.push_back(FloatToHalf(c.X));
            halves.push_back(FloatToHalf(c.Y));
            halves.push_back(FloatToHalf(c.Z));
            halves.push_back(FloatToHalf(c.W));
        }
    };
    plane([](const ProbeShL1& p) -> const Vec4& { return p.R; });
    plane([](const ProbeShL1& p) -> const Vec4& { return p.G; });
    plane([](const ProbeShL1& p) -> const Vec4& { return p.B; });
    return halves;
}

std::vector<BakeTriangle> AssembleProbeBakeTriangles(
    std::vector<BakeTriangle> zoneTriangles,
    const Aabb3d& zoneBounds,
    std::span<const ProbeHaloZone> halo,
    float maxRayDistance)
{
    const Aabb3d reach = Aabb3d::FromCenterHalfExtent(
        zoneBounds.Center(),
        zoneBounds.HalfExtent() + Vec3d(maxRayDistance, maxRayDistance, maxRayDistance));

    std::vector<const ProbeHaloZone*> selected;
    for (const ProbeHaloZone& zone : halo)
        if (reach.Intersects(zone.Bounds))
            selected.push_back(&zone);
    std::stable_sort(selected.begin(), selected.end(),
                     [](const ProbeHaloZone* a, const ProbeHaloZone* b)
                     { return a->ContentHash < b->ContentHash; });

    for (const ProbeHaloZone* zone : selected)
        zoneTriangles.insert(zoneTriangles.end(),
                             zone->Triangles.begin(), zone->Triangles.end());
    return zoneTriangles;
}
