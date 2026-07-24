#pragma once

#include <assets/cook/BrushClustering.h> // CookBrushGeometry
#include <core/assets/AssetRef.h>

#include <vector>

class EditorScene;

//=============================================================================
// CollectCookBrushes — the single bridge from authored brushes to the cook.
//
// Walks the scene's visible brushes, tessellates each through BrushTessellate
// (the exact triangles the editor previews), and emits one CookBrushGeometry per
// brush: its faces with effective materials resolved against the level default,
// plus its world AABB. Grouping is per brush (not a flat face list) because the
// cook clusters whole brushes into spatial cells, so brush boundaries must
// survive collection. Both PIE (in-memory) and the offline level cook build their
// input through here, so they can never cook geometry that disagrees with the
// editor or with each other.
// (docs/plans/sencha-level-editor/05-level-cook.md §2, §7, Revision step 3)
//=============================================================================
// Chart data collected alongside the brushes when the cook bakes lighting:
// one world-unit extent per document-global chart index, the packer's input.
struct CookChartSet
{
    std::vector<Vec2d> Extents;
};

// `charts` non-null turns on lightmap chart generation: every emitted face is
// stamped with a document-global chart id and per-vertex chart-space UVs
// (BrushLightmapCharts), and `charts` receives each chart's extent.
[[nodiscard]] std::vector<CookBrushGeometry> CollectCookBrushes(const EditorScene& scene,
                                                                const AssetRef& levelDefault,
                                                                CookChartSet* charts = nullptr,
                                                                float chartConeDegrees = 45.0f,
                                                                float luxelSize = 0.25f);
