#pragma once

#include <assets/cook/DirectLightBake.h>

#include <cstdint>

// Tuning for the level cook's baked-lighting pass: chart generation, atlas
// packing, and the luxel bake. The editor surfaces these as
// editor.cook.lightmap_* cvars; they fold into the cook hash whenever the
// document has Direct lights, so retuning restales the level.
struct LightmapCookParams
{
    // World units per lightmap texel. The packer may coarsen this when the
    // atlas overflows MaxAtlasSize (logged as EffectiveLuxelSize).
    float LuxelSize = 0.25f;
    // Per-dimension atlas cap; keep <= 4096 so unorm16 vertex UVs stay
    // sub-texel accurate.
    std::uint32_t MaxAtlasSize = 2048;
    // Chart normal-cone split limit (degrees): faces join a chart while their
    // normal stays inside this cone of the chart's area-weighted normal.
    float ConeDegrees = 45.0f;
    DirectLightBakeParams Shading;
};
