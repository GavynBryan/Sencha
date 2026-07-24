#pragma once

#include "DocumentCookSnapshot.h"

#include <assets/cook/BrushClustering.h>
#include <assets/cook/LightingCookParams.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <core/json/JsonValue.h>

#include <string>
#include <vector>

class LoggingProvider;

// Packs one zone atlas over the brush charts and the placement rects, then writes
// the resolved atlas UVs back into the cell face triangles and the per-placement
// lightmap scale/bias into the passthrough scene. This runs before the per-cell
// mesh bake because the mesh weld compares whole vertices: identical UVs weld a
// chart interior and differing UVs split a chart border, with no separate
// chart-splitting pass. Mutates `cells` (face UVs), `placements` (chart index),
// and `passthroughScene` (placement scale/bias); fills `outLayout`.
[[nodiscard]] bool LayoutLightmapSurfaces(
    std::vector<BrushCell>& cells,
    std::vector<LightmapPlacement>& placements,
    const CookChartSet& charts,
    const LightingCookParams& params,
    JsonValue& passthroughScene,
    LoggingProvider& logging,
    LightmapAtlasLayout& outLayout,
    std::string* error = nullptr);
