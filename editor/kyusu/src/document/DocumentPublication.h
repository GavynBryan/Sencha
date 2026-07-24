#pragma once

#include "CookReceipt.h"
#include "DocumentCook.h"
#include "DocumentCookFingerprints.h"
#include "DocumentCookPaths.h"
#include "DocumentCookSnapshot.h"
#include "DocumentPublicationPlan.h"

#include <assets/cook/CookedCache.h>
#include <assets/cook/ImportOnDemand.h>
#include <project/CookProfile.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

struct DocumentCookContext;
struct DocumentCookReuse;

// Builds and stages the cooked-cache index entry for this cook: publishes the
// prepared referenced-asset imports through the document transaction (rejecting a
// destination shared with a generated artifact), content-hashes every published
// artifact (in place for preserved artifacts, from staging for produced ones),
// and stages the updated index. Nothing commits here; a failure leaves the staged
// transaction to roll back. Returns false with the context result carrying the error.
[[nodiscard]] bool StageDocumentIndex(
    const DocumentCookContext& ctx,
    std::string_view sourceRel,
    std::uint64_t documentHash,
    bool runReferencedAssets,
    PendingAssetImport pendingImports);

// Builds and stages the document cook receipt: the published output families
// (produced or preserved per the plan), the withdrawal of any family the profile
// suppresses, and one receipt per cook step (brush cells, surfaces, occlusion,
// and the produced direct/AO/probe artifacts cached for reuse) plus the
// publication step. Consumes the catalog's artifact list into the publication
// receipt. Returns false with the context result carrying the error.
[[nodiscard]] bool StageDocumentReceipt(
    const DocumentCookContext& ctx,
    std::string_view sourceRel,
    std::uint64_t documentHash,
    const CookProfile& profile,
    const DocumentCookFingerprints& fingerprints,
    const DocumentCookReuse& reuse,
    const DocumentPublicationPlan& plan,
    const std::optional<CookedArtifact>& directArtifact,
    const std::optional<CookedArtifact>& aoArtifact,
    const std::optional<CookedArtifact>& probeArtifact,
    bool hasCachedReceipt,
    const DocumentCookReceipt& cachedReceipt);
