#pragma once

#include <assets/cook/LightmapAtlasPack.h>
#include <math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>

class BakeBvh;

//=============================================================================
// Stable lightmap surface coverage. Dev-only (SENCHA_ENABLE_COOK), pure.
//
// A chart's triangles are rasterized once in chart grid space (grid-point
// convention: samples sit AT integer grid coordinates, matching the packer).
// Each covered grid point resolves the world position and shading normal of the
// subsamples that land on the chart's own surface and are not buried. The result
// is the shared input for both the direct-light bake and the AO bake: neither
// evaluator resolves geometry, so a change to one output never rebuilds the
// sample map. Sampling is one-sided (the span holds only the chart's own
// surface) so a boundary luxel bakes the limit of its own chart's lighting.
//=============================================================================

struct LightmapRasterTriangle
{
    // Chart grid coordinates (world units / luxel), relative to grid point
    // (0,0) of the chart's rect; the packer's gutter is applied internally.
    Vec2d Uv[3];
    Vec3d Position[3]; // world space
    Vec3d Normal[3];   // world space, smoothed shading normals
};

// One resolved subsample: the world position and shading normal a bake evaluates
// at. Direct light averages over a grid point's samples in subsample order; AO
// uses the first.
struct LightmapSurfaceSample
{
    Vec3d Position;
    Vec3d Normal;
};

// Per-chart resolved coverage in grid space (rect minus the packer gutter),
// row-major. CSR-style: grid point i owns samples [Offsets[i], Offsets[i+1]);
// covered iff that range is non-empty. GridWidth == 0 means the rect was too
// small to hold any grid point (nothing to bake).
struct LightmapSurfaceSamples
{
    std::uint32_t                      GridWidth = 0;
    std::uint32_t                      GridHeight = 0;
    std::vector<LightmapSurfaceSample> Samples;
    std::vector<std::uint32_t>         Offsets;
};

// Resolves one chart's surface samples: rasterizes the triangles, and for each
// grid point records the subsamples (in subsample order) that resolve onto the
// chart and survive the buried-geometry test. `normalOffset` lifts the buried
// probe origin off the sample's own surface, matching the radiance bake's
// receiver offset.
[[nodiscard]] LightmapSurfaceSamples ResolveLightmapSurfaceSamples(
    std::span<const LightmapRasterTriangle> triangles,
    const LightmapChartRect& rect,
    const BakeBvh& occluders,
    float normalOffset);
