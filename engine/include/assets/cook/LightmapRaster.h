#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <assets/cook/AmbientOcclusionBake.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <assets/cook/LightmapSurfaceSamples.h>
#include <math/Vec.h>

class BakeBvh;

//=============================================================================
// Lightmap luxel bake over a resolved surface-sample map. Dev-only
// (SENCHA_ENABLE_COOK), pure.
//
// Coverage is resolved once (ResolveLightmapSurfaceSamples): each covered grid
// point holds the world positions and shading normals of its contributing
// subsamples. The direct bake averages baked radiance over those subsamples; the
// AO bake evaluates hemisphere visibility at the first. Both dilate their own
// channel from covered neighbors over the shared coverage mask, so bilinear
// filtering never reads an unlit gutter, and neither output depends on the
// other. `BakeChartLuxels` composes resolve + direct + optional AO for callers
// that want both in one pass.
//=============================================================================

// Bakes direct radiance for one chart's resolved samples into `atlasPixels`
// (row-major RGB9E5, atlasWidth texels per row). Touches only the chart's
// padded rect; texels dilation never reaches encode to black.
void BakeDirectLightmapChart(const LightmapSurfaceSamples& samples,
                             const LightmapChartRect& rect,
                             std::span<const BakeDirectLight> lights,
                             const BakeBvh& occluders,
                             const DirectLightBakeParams& params,
                             std::uint32_t atlasWidth,
                             std::span<std::uint32_t> atlasPixels);

// Bakes ambient occlusion for one chart's resolved samples into `aoPixels`
// (row-major R8 unorm, same atlas layout): covered and dilated texels are
// written, everything else keeps the caller's fill (white, so texels no chart
// reaches never darken the ambient term).
void BakeAmbientOcclusionChart(const LightmapSurfaceSamples& samples,
                               const LightmapChartRect& rect,
                               const BakeBvh& occluders,
                               const AmbientOcclusionBakeParams& aoParams,
                               std::uint32_t atlasWidth,
                               std::span<std::uint8_t> aoPixels);

// Resolves one chart and bakes direct radiance, plus AO when `aoParams` is set,
// in one call. Byte-identical to resolving the chart and calling the two chart
// bakes above.
void BakeChartLuxels(std::span<const LightmapRasterTriangle> triangles,
                     const LightmapChartRect& rect,
                     std::span<const BakeDirectLight> lights,
                     const BakeBvh& occluders,
                     const DirectLightBakeParams& params,
                     std::uint32_t atlasWidth,
                     std::span<std::uint32_t> atlasPixels,
                     const AmbientOcclusionBakeParams* aoParams = nullptr,
                     std::span<std::uint8_t> aoPixels = {});
