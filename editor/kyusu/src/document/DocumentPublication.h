#pragma once

#include "CookReceipt.h"
#include "DocumentCook.h"
#include "DocumentCookFingerprints.h"
#include "DocumentCookPaths.h"
#include "DocumentCookSnapshot.h"

#include <assets/cook/CookedCache.h>
#include <assets/cook/ImportOnDemand.h>
#include <project/CookProfile.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

class CookArtifactTransaction;
class DocumentArtifactCatalog;
struct DocumentCookReuse;

// Builds and stages the cooked-cache index entry for this cook: publishes the
// prepared referenced-asset imports through the document transaction (rejecting a
// destination shared with a generated artifact), carries a preserved collision
// artifact forward when collision is not refreshed, content-hashes every
// published artifact, and stages the updated index. Nothing commits here; a
// failure leaves the staged transaction to roll back. Returns false with `result`
// carrying the error.
[[nodiscard]] bool StageDocumentIndex(
    std::string_view sourceRel,
    std::uint64_t documentHash,
    bool runReferencedAssets,
    bool emitCollision,
    PendingAssetImport pendingImports,
    DocumentArtifactCatalog& catalog,
    const DocumentCookPaths& paths,
    const std::filesystem::path& assetsRoot,
    CookArtifactTransaction& transaction,
    DocumentCookResult& result);

// Builds and stages the document cook receipt: the published output families, the
// withdrawal of any family the profile suppresses, and one receipt per cook step
// (brush cells, surfaces, occlusion, and the produced direct/AO/probe artifacts
// cached for reuse) plus the publication step. Consumes the catalog's artifact
// list into the publication receipt. Returns false with `result` carrying the
// error.
[[nodiscard]] bool StageDocumentReceipt(
    std::string_view sourceRel,
    std::uint64_t documentHash,
    const CookProfile& profile,
    const DocumentCookSnapshot& snapshot,
    const DocumentCookFingerprints& fingerprints,
    const DocumentCookReuse& reuse,
    DocumentArtifactCatalog& catalog,
    const std::optional<CookedArtifact>& directArtifact,
    const std::optional<CookedArtifact>& aoArtifact,
    const std::optional<CookedArtifact>& probeArtifact,
    const DocumentCookPaths& paths,
    const std::filesystem::path& assetsRoot,
    std::string_view stem,
    bool hasCachedReceipt,
    const DocumentCookReceipt& cachedReceipt,
    CookArtifactTransaction& transaction,
    DocumentCookResult& result);
