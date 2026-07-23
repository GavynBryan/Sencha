#pragma once

#include "DocumentCook.h"
#include "DocumentCookSnapshot.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <core/json/JsonValue.h>

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

struct DocumentCookContext;
struct DocumentCookReuse;

// Resolves the zone lightmap: the direct-light atlas and, when enabled, the AO
// plane. A reusable prior bake is restored through the transaction; otherwise the
// chart triangles are gathered from the cell faces and placements, rasterized and
// baked over the occlusion BVH, and written as cooked textures. Either way it
// appends the ZoneLightmap entity to `cellEntities`, records the atlas artifacts
// in `catalog`, sets `directArtifact`/`aoArtifact`, and stamps the atlas
// dimensions and reused-step metadata on the context result. Owns the
// DirectLightmap (and, when AO is enabled, AmbientOcclusion) progress steps.
// Returns false with the context result carrying the error or cancellation.
[[nodiscard]] bool BakeDocumentLightmap(
    const DocumentCookContext& ctx,
    const DocumentCookSnapshot& snapshot,
    const std::vector<BrushCell>& cells,
    const LightmapAtlasLayout& atlasLayout,
    const BakeBvh& occlusionBvh,
    const DocumentCookReuse& reuse,
    JsonValue::Array& cellEntities,
    std::optional<CookedArtifact>& directArtifact,
    std::optional<CookedArtifact>& aoArtifact);
