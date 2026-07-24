#include <assets/cook/LightmapRaster.h>

#include <assets/cook/BakeBvh.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

void BakeDirectLightmapChart(const LightmapSurfaceSamples& samples,
                             const LightmapChartRect& rect,
                             std::span<const BakeDirectLight> lights,
                             const BakeBvh& occluders,
                             const DirectLightBakeParams& params,
                             std::uint32_t atlasWidth,
                             std::span<std::uint32_t> atlasPixels)
{
    if (samples.GridWidth == 0 || samples.GridHeight == 0)
        return;
    const std::uint32_t gridW = samples.GridWidth;
    const std::uint32_t gridH = samples.GridHeight;

    // Padded-rect radiance buffer: covered grid points bake, then dilation fills
    // the gutter (and any uncovered interior gaps) from covered neighbors so
    // bilinear filtering reads plausible values everywhere.
    struct Texel
    {
        Vec3d Radiance{ 0.0f, 0.0f, 0.0f };
        bool  Covered = false;
    };
    std::vector<Texel> texels(static_cast<std::size_t>(rect.Width) * rect.Height);

    for (std::uint32_t y = 0; y < gridH; ++y)
        for (std::uint32_t x = 0; x < gridW; ++x)
        {
            const std::size_t gi = static_cast<std::size_t>(y) * gridW + x;
            const std::uint32_t begin = samples.Offsets[gi];
            const std::uint32_t end = samples.Offsets[gi + 1];
            if (begin == end)
                continue;
            Vec3d radiance{ 0.0f, 0.0f, 0.0f };
            for (std::uint32_t k = begin; k < end; ++k)
                radiance += EvaluateBakedDirectRadiance(
                    samples.Samples[k].Position, samples.Samples[k].Normal,
                    lights, occluders, params);
            Texel& texel = texels[static_cast<std::size_t>(y + kLightmapGutter) * rect.Width
                + x + kLightmapGutter];
            texel.Radiance = radiance / static_cast<float>(end - begin);
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
            const std::size_t atlasIndex =
                static_cast<std::size_t>(rect.Y + y) * atlasWidth + rect.X + x;
            atlasPixels[atlasIndex] = EncodeBakedDirectRgb9e5(texel.Radiance);
        }
}

void BakeAmbientOcclusionChart(const LightmapSurfaceSamples& samples,
                               const LightmapChartRect& rect,
                               const BakeBvh& occluders,
                               const AmbientOcclusionBakeParams& aoParams,
                               std::uint32_t atlasWidth,
                               std::span<std::uint8_t> aoPixels)
{
    if (samples.GridWidth == 0 || samples.GridHeight == 0)
        return;
    const std::uint32_t gridW = samples.GridWidth;
    const std::uint32_t gridH = samples.GridHeight;

    struct Texel
    {
        float Ao = 0.0f;
        bool  Covered = false;
    };
    std::vector<Texel> texels(static_cast<std::size_t>(rect.Width) * rect.Height);

    // AO is a smooth field at luxel scale, so one hemisphere estimate per luxel
    // suffices (the supersampling exists for razor-sharp direct shadow
    // boundaries). The first resolved subsample is the deterministic choice.
    for (std::uint32_t y = 0; y < gridH; ++y)
        for (std::uint32_t x = 0; x < gridW; ++x)
        {
            const std::size_t gi = static_cast<std::size_t>(y) * gridW + x;
            const std::uint32_t begin = samples.Offsets[gi];
            if (begin == samples.Offsets[gi + 1])
                continue;
            const LightmapSurfaceSample& first = samples.Samples[begin];
            Texel& texel = texels[static_cast<std::size_t>(y + kLightmapGutter) * rect.Width
                + x + kLightmapGutter];
            texel.Ao = EvaluateAmbientOcclusion(first.Position, first.Normal,
                                                occluders, aoParams);
            texel.Covered = true;
        }

    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<Texel> next = texels;
        for (std::uint32_t y = 0; y < rect.Height; ++y)
            for (std::uint32_t x = 0; x < rect.Width; ++x)
            {
                Texel& out = next[static_cast<std::size_t>(y) * rect.Width + x];
                if (out.Covered)
                    continue;
                float sum = 0.0f;
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
                        sum += neighbor.Ao;
                        ++count;
                    }
                if (count > 0)
                {
                    out.Ao = sum / static_cast<float>(count);
                    out.Covered = true;
                }
            }
        texels = std::move(next);
    }

    for (std::uint32_t y = 0; y < rect.Height; ++y)
        for (std::uint32_t x = 0; x < rect.Width; ++x)
        {
            const Texel& texel = texels[static_cast<std::size_t>(y) * rect.Width + x];
            // Texels dilation never reached keep the caller's white fill: an AO
            // of 0 there would darken, not disappear, under filtering.
            if (!texel.Covered)
                continue;
            const std::size_t atlasIndex =
                static_cast<std::size_t>(rect.Y + y) * atlasWidth + rect.X + x;
            aoPixels[atlasIndex] = static_cast<std::uint8_t>(
                std::lround(std::clamp(texel.Ao, 0.0f, 1.0f) * 255.0f));
        }
}

void BakeChartLuxels(std::span<const LightmapRasterTriangle> triangles,
                     const LightmapChartRect& rect,
                     std::span<const BakeDirectLight> lights,
                     const BakeBvh& occluders,
                     const DirectLightBakeParams& params,
                     std::uint32_t atlasWidth,
                     std::span<std::uint32_t> atlasPixels,
                     const AmbientOcclusionBakeParams* aoParams,
                     std::span<std::uint8_t> aoPixels)
{
    const LightmapSurfaceSamples samples =
        ResolveLightmapSurfaceSamples(triangles, rect, occluders, params.NormalOffset);
    BakeDirectLightmapChart(samples, rect, lights, occluders, params, atlasWidth,
                            atlasPixels);
    if (aoParams != nullptr && !aoPixels.empty())
        BakeAmbientOcclusionChart(samples, rect, occluders, *aoParams, atlasWidth,
                                  aoPixels);
}
