#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <assets/cook/AmbientOcclusionBake.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <math/Vec.h>

class BakeBvh;

//=============================================================================
// Lightmap luxel rasterization + bake. Dev-only (SENCHA_ENABLE_COOK), pure.
//
// A chart's triangles are rasterized in chart grid space (grid-point
// convention: samples sit AT integer grid coordinates, matching the packer).
// Expanded triangle bounds build a deterministic candidate list per grid
// point. Each of its four supersamples independently selects a containing
// triangle, or the nearest edge-clamped candidate within half a luxel
// diagonal, then interpolates its world position and authored shading normal.
// Sampling is one-sided: the span holds only the chart's own surface, so a
// boundary luxel bakes the limit of its own chart's lighting. Coplanar charts
// share a world-aligned luxel lattice, so where the lighting is continuous
// across a chart or mesh seam the one-sided limits agree, and where an
// illumination step lies exactly on the seam each side keeps its own value
// instead of averaging the two into the shared boundary texels. Uncovered
// texels near covered ones are dilated so bilinear filtering never reads an
// unlit gutter. Open, one-sided shells are valid receivers; authored normals
// control light reception while bake occlusion remains two-sided.
//=============================================================================

struct LightmapRasterTriangle
{
    // Chart grid coordinates (world units / luxel), relative to grid point
    // (0,0) of the chart's rect; the packer's gutter is applied internally.
    Vec2d Uv[3];
    Vec3d Position[3]; // world space
    Vec3d Normal[3];   // world space, smoothed shading normals
};

// Rasterizes and bakes one chart into `atlasPixels` (row-major RGB9E5,
// atlasWidth texels per row). Touches only the chart's padded rect.
//
// With `aoParams` set, the same resolved samples also evaluate ambient
// occlusion into `aoPixels` (row-major R8 unorm, same atlas layout): covered
// and dilated texels are written, everything else keeps the caller's fill
// (white, so texels no chart reaches never darken the ambient term).
void BakeChartLuxels(std::span<const LightmapRasterTriangle> triangles,
                     const LightmapChartRect& rect,
                     std::span<const BakeDirectLight> lights,
                     const BakeBvh& occluders,
                     const DirectLightBakeParams& params,
                     std::uint32_t atlasWidth,
                     std::span<std::uint32_t> atlasPixels,
                     const AmbientOcclusionBakeParams* aoParams = nullptr,
                     std::span<std::uint8_t> aoPixels = {});
