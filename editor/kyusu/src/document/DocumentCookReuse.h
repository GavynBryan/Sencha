#pragma once

#include "CookReceipt.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "DocumentCook.h"
#include "DocumentCookFingerprints.h"
#include "DocumentCookPaths.h"
#include "DocumentCookRequest.h"
#include "DocumentCookSnapshot.h"

#include <assets/cook/CookedCache.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

// Reuse resolution for one document cook. Both forms read cooked-cache state
// only and run no cook work: the coarsest is a whole-document cache hit that
// republishes nothing; the finer is a per-lighting-step reuse of a prior
// receipt's still-valid direct/AO/probe artifact.

// A whole-document cache hit: the resolved identity matches the active
// publication and every published artifact is on disk and unchanged. Returns the
// filled result (and finishes progress) on a hit, or nullopt to cook.
[[nodiscard]] inline std::optional<DocumentCookResult> TryDocumentCookCacheHit(
    const DocumentCookRequest& request, const DocumentCookPaths& paths,
    const std::filesystem::path& assetsRoot, std::string_view sourceRel,
    std::uint64_t documentHash, bool referencedAssetsFresh,
    bool hasCachedReceipt, const DocumentCookReceipt& cachedReceipt,
    CookStepProgress& progress)
{
    if (request.ForceRebuild || !referencedAssetsFresh || !hasCachedReceipt
        || cachedReceipt.PublishedProfileId != request.Profile.Id
        || cachedReceipt.PublishedFingerprint != documentHash)
        return std::nullopt;

    CookedCacheIndex cachedIndex;
    std::error_code ec;
    if (!CookedCacheIndex::LoadFromFile(paths.Index.generic_string(), cachedIndex)
        || !std::filesystem::exists(paths.Scene, ec))
        return std::nullopt;

    const CookedSourceEntry* cached = cachedIndex.Find(sourceRel);
    if (cached == nullptr || cached->InputFingerprint != documentHash
        || !CookedSourceArtifactsMatch(assetsRoot, *cached))
        return std::nullopt;

    DocumentCookResult result;
    result.Success = true;
    result.CacheHit = true;
    result.ReusedSteps = request.Graph.OrderedSteps;
    result.ProfileId = request.Profile.Id;
    result.ContentHash = documentHash;
    result.CookedScenePath = paths.Scene;
    if (const CookStepReceipt* publication =
            FindCookStepReceipt(cachedReceipt, DocumentCookStepIds::Publication))
        RestoreDocumentCookResultMetadata(publication->Metadata, result);
    for (const CookedArtifact& artifact : cached->Artifacts)
        if (artifact.Type == AssetType::StaticMesh)
            result.GeneratedMeshPaths.push_back(artifact.Path);
    progress.Finish();
    return result;
}

// The prior receipt's reusable lighting steps, resolved against this cook's
// fingerprints. A step is reusable only if its input is present this cook (no
// bake lights means no direct reuse) and its fingerprint still matches.
struct DocumentCookReuse
{
    const CookStepReceipt* Direct = nullptr;
    const CookStepReceipt* Ao = nullptr;
    const CookStepReceipt* Probes = nullptr;
    // Direct is reusable and, if AO is enabled, AO is reusable too: lighting
    // republishes nothing this cook.
    bool ReuseLighting = false;
};

[[nodiscard]] inline DocumentCookReuse ResolveDocumentCookReuse(
    const DocumentCookReceipt* priorReceipt, const std::filesystem::path& assetsRoot,
    const DocumentCookSnapshot& snapshot, const DocumentCookFingerprints& fingerprints)
{
    DocumentCookReuse reuse;
    reuse.Direct = !snapshot.BakeLights.empty()
        ? FindReusableCookStep(priorReceipt, assetsRoot, CookStepIds::DirectLightmap,
                               fingerprints.Direct)
        : nullptr;
    reuse.Ao = snapshot.Lighting.Ao.Enabled
        ? FindReusableCookStep(priorReceipt, assetsRoot, CookStepIds::AmbientOcclusion,
                               fingerprints.Ao)
        : nullptr;
    reuse.ReuseLighting = reuse.Direct != nullptr
        && (!snapshot.Lighting.Ao.Enabled || reuse.Ao != nullptr);
    reuse.Probes = !snapshot.ProbeVolumes.empty()
        ? FindReusableCookStep(priorReceipt, assetsRoot, CookStepIds::IrradianceProbes,
                               fingerprints.Probe)
        : nullptr;
    return reuse;
}
