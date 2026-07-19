#pragma once

#include <assets/cook/DirectLightBake.h>
#include <assets/cook/ProbeBake.h>

#include <cstdint>

// Tuning for the level cook's lighting bake: lightmap chart generation, atlas
// packing, the luxel bake, and the irradiance-probe volume bake. The editor
// surfaces these as editor.cook.lightmap_* / editor.cook.probe_* cvars plus
// the render.ambient.* pair; each group folds into the cook hash only when
// the document has the inputs it affects (Direct lights for the lightmap,
// authored volumes for probes), so retuning restales exactly what it changes.
struct LightingCookParams
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

    // Irradiance-probe bake (renderer plan 7 / 3B). Probe.Shading mirrors
    // Shading at the call site so the bounce matches the lightmap term;
    // Probe.SkyColor/GroundColor carry the render.ambient.* values the bake
    // replaces at runtime.
    ProbeBakeParams Probe;
    std::uint32_t ProbeRayCount = 128;
};
