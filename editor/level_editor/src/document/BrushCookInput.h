#pragma once

#include "brush/BrushEvaluation.h"

#include <assets/cook/BrushClustering.h> // CookBrushGeometry
#include <core/assets/AssetRef.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <vector>

class EditorScene;

//=============================================================================
// CollectCookBrushes — the single bridge from authored brushes to the cook.
//
// Walks the scene's visible brushes, evaluates each through its modifier stack,
// tessellates every evaluated piece through BrushTessellate (the exact
// triangles the editor previews), and emits one CookBrushGeometry per piece:
// its faces with effective materials resolved against the level default, plus
// its world AABB. Grouping is per piece (not a flat face list) because the cook
// clusters whole pieces into spatial cells, so piece boundaries must survive
// collection. Both PIE (in-memory) and the offline level cook build their
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

// A brush whose modifier stack could not be evaluated under the policy: the
// cook must refuse rather than ship the partial result.
struct CookBrushFailure
{
    EntityId      Entity;
    std::uint32_t Modifier = 0; // index into the brush's modifier stack
};

// One mesh at `transform`, tessellated into cook faces with materials resolved
// against the level default, plus its bounds in that frame. The one tessellation
// the cook and the viewport's bake share: the viewport bakes each distinct mesh
// once at identity and places it per piece, the cook flattens every piece in
// world space, and both go through here so they can never disagree about a
// triangle. `charts` as for CollectCookBrushes.
[[nodiscard]] CookBrushGeometry CollectBrushGeometry(const BrushMesh& mesh,
                                                     const Transform3f& transform,
                                                     const AssetRef& levelDefault,
                                                     CookChartSet* charts = nullptr,
                                                     float chartConeDegrees = 45.0f,
                                                     float luxelSize = 0.25f);

// `charts` non-null turns on lightmap chart generation: every emitted face is
// stamped with a document-global chart id and per-vertex chart-space UVs
// (BrushLightmapCharts), and `charts` receives each chart's extent. `policy`
// is the evaluation limit: the cook's deterministic hard limit by default, the
// editor's interactive one for the viewport preview. Brushes that fail it emit
// what evaluated and are listed in `failures` when given.
[[nodiscard]] std::vector<CookBrushGeometry> CollectCookBrushes(
    const EditorScene& scene,
    const AssetRef& levelDefault,
    CookChartSet* charts = nullptr,
    float chartConeDegrees = 45.0f,
    float luxelSize = 0.25f,
    const BrushEvaluationPolicy& policy = BrushEvaluationPolicy::Cook(),
    std::vector<CookBrushFailure>* failures = nullptr);
