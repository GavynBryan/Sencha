#pragma once

#include "DocumentCook.h"
#include "DocumentCookSnapshot.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/CookedCache.h>

#include <filesystem>
#include <optional>
#include <string_view>

struct DocumentCookContext;
struct DocumentCookReuse;

// Resolves the zone's irradiance-probe volumes into one .sprobe. A reusable prior
// bake is restored through the transaction; otherwise each authored volume is
// baked over the occlusion BVH (bounce from direct and indirect lights) and the
// packed volumes are written as one cooked artifact. Records the artifact in
// `catalog`, sets `probeArtifact`, and stamps the probe counts and reused-step
// metadata on the context result. Owns the IrradianceProbes progress step and
// the per-volume cancellation check. Returns false with the context result
// carrying the error or cancellation.
[[nodiscard]] bool BakeDocumentProbes(
    const DocumentCookContext& ctx,
    const DocumentCookSnapshot& snapshot,
    const BakeBvh& occlusionBvh,
    const DocumentCookReuse& reuse,
    std::optional<CookedArtifact>& probeArtifact);
