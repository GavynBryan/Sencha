#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <math/Vec.h>

class BakeBvh;

//=============================================================================
// Lightmap luxel rasterization + bake. Dev-only (SENCHA_ENABLE_COOK), pure.
//
// A chart's triangles are rasterized in chart grid space (grid-point
// convention: samples sit AT integer grid coordinates, matching the packer).
// Each covered grid point records a barycentric-interpolated world position
// and smoothed shading normal, then bakes through the shared per-sample
// evaluator (EvaluateBakedDirectRadiance). Uncovered texels near covered ones
// are dilated so bilinear filtering never reads unlit gutter. Deterministic:
// triangles rasterize in input order, interior coverage beats edge coverage,
// and within a class the first writer wins.
//=============================================================================

struct LightmapRasterTriangle
{
    // Chart grid coordinates (world units / luxel), relative to grid point
    // (0,0) of the chart's rect; the packer's gutter is applied internally.
    Vec2d Uv[3];
    Vec3d Position[3]; // world space
    Vec3d Normal[3];   // world space, smoothed shading normals
};

// Rasterizes and bakes one chart into `atlasPixels` (row-major RGBA8/RGBM,
// atlasWidth texels per row). Touches only the chart's padded rect.
void BakeChartLuxels(std::span<const LightmapRasterTriangle> triangles,
                     const LightmapChartRect& rect,
                     std::span<const BakeDirectLight> lights,
                     const BakeBvh& occluders,
                     const DirectLightBakeParams& params,
                     std::uint32_t atlasWidth,
                     std::span<std::uint32_t> atlasPixels);
