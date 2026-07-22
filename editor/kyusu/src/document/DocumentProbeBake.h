#pragma once

#include "DocumentCook.h"
#include "DocumentCookSnapshot.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/CookedCache.h>

#include <filesystem>
#include <optional>
#include <string_view>

class CookArtifactTransaction;
class CookStepProgress;
class DocumentArtifactCatalog;
struct DocumentCookReuse;

// Resolves the zone's irradiance-probe volumes into one .sprobe. A reusable prior
// bake is restored through the transaction; otherwise each authored volume is
// baked over the occlusion BVH (bounce from direct and indirect lights) and the
// packed volumes are written as one cooked artifact. Records the artifact in
// `catalog`, sets `probeArtifact`, and stamps the probe counts and reused-step
// metadata on `result`. Owns the IrradianceProbes progress step and the
// per-volume cancellation check. Returns false with `result` carrying the error
// or cancellation.
[[nodiscard]] bool BakeDocumentProbes(
    const DocumentCookSnapshot& snapshot,
    const BakeBvh& occlusionBvh,
    const DocumentCookReuse& reuse,
    const std::filesystem::path& assetsRoot,
    std::string_view stem,
    CookArtifactTransaction& transaction,
    DocumentArtifactCatalog& catalog,
    CookStepProgress& progress,
    std::optional<CookedArtifact>& probeArtifact,
    DocumentCookResult& result);
