#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <math/Vec.h>

//=============================================================================
// Lightmap atlas packing for the level cook. Dev-only (SENCHA_ENABLE_COOK),
// pure. Charts sample at luxel GRID POINTS (a chart spanning N luxels stores
// N+1 texels per axis), each padded by a fixed gutter that dilation fills so
// bilinear filtering never reads a neighboring chart. Texel (0,0) and the
// whole first row/column are reserved black: an item with a zero UV
// scale/bias collapses every sample there, which decodes to additive zero.
//
// Deterministic: placement order is (height desc, width desc, chart index
// asc); overflow first grows the atlas (power of two, up to maxSize), then
// coarsens the effective luxel size by sqrt(2) steps until everything fits.
//=============================================================================

// Padded gutter texels on every chart side, filled by dilation. Fixed: 2
// covers the bilinear reach (0.5) plus unorm16 UV quantization with margin.
inline constexpr std::uint32_t kLightmapGutter = 2;

struct LightmapChartRect
{
    // Padded rect origin/size in atlas texels (gutter included). The chart's
    // grid point (0,0) lives at texel (X + gutter, Y + gutter).
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
};

struct LightmapAtlasLayout
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Equals the requested luxel size unless the max atlas size forced a
    // coarsening; the cook logs when they differ.
    float EffectiveLuxelSize = 0.0f;
    std::vector<LightmapChartRect> Rects; // indexed by input chart order
};

// chartExtents are world-unit chart AABB sizes (UvMax - UvMin per chart).
// maxSize caps both atlas dimensions; keep it <= 4096 so unorm16 vertex UVs
// stay sub-texel accurate.
[[nodiscard]] LightmapAtlasLayout PackLightmapAtlas(std::span<const Vec2d> chartExtents,
                                                    float luxelSize,
                                                    std::uint32_t maxSize);
