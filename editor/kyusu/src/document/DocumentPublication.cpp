#include "DocumentPublication.h"

#include "CookArtifactTransaction.h"
#include "CookGraph.h"
#include "CookStepCache.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookReuse.h"
#include "DocumentImportPublisher.h"

#include <core/hash/ContentHash.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

bool StageDocumentIndex(std::string_view sourceRel, std::uint64_t documentHash,
                        bool runReferencedAssets, PendingAssetImport pendingImports,
                        DocumentArtifactCatalog& catalog, const DocumentCookPaths& paths,
                        const std::filesystem::path& assetsRoot,
                        CookArtifactTransaction& transaction, DocumentCookResult& result)
{
    // The index loads active (prepare left it untouched); the prepared imports,
    // their index deltas, and the level entry then publish through this one staged
    // index and the transaction, committing together.
    CookedCacheIndex index;
    (void)CookedCacheIndex::LoadFromFile(paths.Index.generic_string(), index); // cold cache is fine

    std::string cookError;
    if (runReferencedAssets)
    {
        std::unordered_set<std::string> documentArtifactPaths;
        for (const CookedArtifact& artifact : catalog.Artifacts())
            documentArtifactPaths.insert(artifact.FileRelPath);
        for (const PreparedCookedArtifact& prepared : pendingImports.Artifacts)
            if (documentArtifactPaths.count(prepared.Artifact.FileRelPath) != 0)
            {
                result.Error = "CookDocument: referenced import collides with "
                    "generated artifact '" + prepared.Artifact.FileRelPath + "'";
                return false;
            }
        DocumentImportPublisher publisher(transaction, assetsRoot, index);
        if (!PublishAssetImport(std::move(pendingImports), publisher, &cookError))
        {
            result.Error = "CookDocument: " + cookError;
            return false;
        }
    }
    CookedSourceEntry entry;
    entry.SourceRelPath = std::string(sourceRel);
    entry.InputFingerprint = documentHash;
    for (CookedArtifact& artifact : catalog.Artifacts())
    {
        // Preserved artifacts already exist in the active tree, so hash them in
        // place; produced ones are staged and hashed from staging.
        const std::filesystem::path active = assetsRoot / artifact.FileRelPath;
        const std::filesystem::path contentPath =
            catalog.IsPreserved(artifact.FileRelPath) ? active : transaction.Stage(active);
        std::uint64_t hash = 0;
        if (HashFileContents(contentPath.generic_string(), hash))
            artifact.ContentHash = hash;
    }
    entry.Artifacts = catalog.Artifacts();
    index.Put(std::move(entry));
    if (!transaction.Seed(paths.Index, &cookError)
        || !index.SaveToFile(transaction.Stage(paths.Index).generic_string()))
    {
        result.Error = "CookDocument: could not stage cooked cache index";
        return false;
    }
    return true;
}

bool StageDocumentReceipt(std::string_view sourceRel, std::uint64_t documentHash,
                          const CookProfile& profile, const DocumentCookSnapshot& snapshot,
                          const DocumentCookFingerprints& fingerprints,
                          const DocumentCookReuse& reuse,
                          const DocumentPublicationPlan& plan,
                          DocumentArtifactCatalog& catalog,
                          const std::optional<CookedArtifact>& directArtifact,
                          const std::optional<CookedArtifact>& aoArtifact,
                          const std::optional<CookedArtifact>& probeArtifact,
                          const DocumentCookPaths& paths,
                          const std::filesystem::path& assetsRoot, std::string_view stem,
                          bool hasCachedReceipt, const DocumentCookReceipt& cachedReceipt,
                          CookArtifactTransaction& transaction, DocumentCookResult& result)
{
    const std::string stemStr(stem);
    std::string cookError;
    DocumentCookReceipt receipt = hasCachedReceipt ? cachedReceipt : DocumentCookReceipt{};
    receipt.Target = std::string(sourceRel);
    receipt.PublishedProfileId = profile.Id;
    receipt.PublishedFingerprint = documentHash;
    // A family is published if this cook produced it or preserved a prior output.
    const auto published = [](FamilyPublication state)
    {
        return state == FamilyPublication::Produced
            || state == FamilyPublication::Preserved;
    };
    receipt.PublishedOutputFamilies = {
        std::string(CookOutputFamilies::Structure),
    };
    if (published(plan.Collision))
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::Collision));
    if (CookProfileOutputDisposition(profile, CookOutputFamilies::ReferencedAssets)
        != CookOutputDisposition::Withdraw)
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::ReferencedAssets));
    if (published(plan.DirectLightmap))
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::DirectLightmap));
    if (published(plan.AmbientOcclusion))
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::AmbientOcclusion));
    if (published(plan.IrradianceProbes))
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::IrradianceProbes));

    if (CookProfileOutputDisposition(profile, CookOutputFamilies::DirectLightmap)
        == CookOutputDisposition::Withdraw)
        transaction.Withdraw(assetsRoot / ".cooked/levels" / stemStr / "lightmap.stex");
    if (CookProfileOutputDisposition(profile, CookOutputFamilies::AmbientOcclusion)
        == CookOutputDisposition::Withdraw)
        transaction.Withdraw(assetsRoot / ".cooked/levels" / stemStr / "ao.stex");
    if (CookProfileOutputDisposition(profile, CookOutputFamilies::IrradianceProbes)
        == CookOutputDisposition::Withdraw)
        transaction.Withdraw(assetsRoot / ".cooked/levels" / stemStr / "probes.sprobe");

    PutCookStepReceipt(receipt, CookStepReceipt{
        .StepId = std::string(DocumentCookStepIds::BrushCells),
        .Version = FindDocumentCookStep(DocumentCookStepIds::BrushCells)->Version,
        .InputFingerprint = fingerprints.Brush,
    });
    PutCookStepReceipt(receipt, CookStepReceipt{
        .StepId = std::string(DocumentCookStepIds::LightmapSurfaces),
        .Version = FindDocumentCookStep(DocumentCookStepIds::LightmapSurfaces)->Version,
        .InputFingerprint = fingerprints.LightmapSurfaces,
        .Dependencies = {
            { std::string(DocumentCookStepIds::BrushCells), fingerprints.Brush },
        },
    });
    PutCookStepReceipt(receipt, CookStepReceipt{
        .StepId = std::string(DocumentCookStepIds::OcclusionGeometry),
        .Version = FindDocumentCookStep(DocumentCookStepIds::OcclusionGeometry)->Version,
        .InputFingerprint = fingerprints.Occlusion,
        .Dependencies = {
            { std::string(DocumentCookStepIds::BrushCells), fingerprints.Brush },
        },
    });

    if (directArtifact.has_value() && !reuse.ReuseLighting)
    {
        CookedArtifact cached = CacheCookStepArtifact(
            *directArtifact, sourceRel, CookStepIds::DirectLightmap,
            fingerprints.Direct, assetsRoot, transaction, &cookError);
        if (cached.Path.empty())
        {
            result.Error = "CookDocument: " + cookError;
            return false;
        }
        PutCookStepReceipt(receipt, CookStepReceipt{
            .StepId = std::string(CookStepIds::DirectLightmap),
            .Version = FindDocumentCookStep(CookStepIds::DirectLightmap)->Version,
            .InputFingerprint = fingerprints.Direct,
            .Dependencies = {
                { std::string(DocumentCookStepIds::LightmapSurfaces),
                  fingerprints.LightmapSurfaces },
                { std::string(DocumentCookStepIds::OcclusionGeometry),
                  fingerprints.Occlusion },
            },
            .Artifacts = { std::move(cached) },
            .Metadata = DocumentCookResultMetadata(result),
        });
    }
    if (aoArtifact.has_value() && !reuse.ReuseLighting)
    {
        CookedArtifact cached = CacheCookStepArtifact(
            *aoArtifact, sourceRel, CookStepIds::AmbientOcclusion,
            fingerprints.Ao, assetsRoot, transaction, &cookError);
        if (cached.Path.empty())
        {
            result.Error = "CookDocument: " + cookError;
            return false;
        }
        PutCookStepReceipt(receipt, CookStepReceipt{
            .StepId = std::string(CookStepIds::AmbientOcclusion),
            .Version = FindDocumentCookStep(CookStepIds::AmbientOcclusion)->Version,
            .InputFingerprint = fingerprints.Ao,
            .Dependencies = {
                { std::string(DocumentCookStepIds::LightmapSurfaces),
                  fingerprints.LightmapSurfaces },
                { std::string(DocumentCookStepIds::OcclusionGeometry),
                  fingerprints.Occlusion },
            },
            .Artifacts = { std::move(cached) },
            .Metadata = DocumentCookResultMetadata(result),
        });
    }
    if (probeArtifact.has_value() && reuse.Probes == nullptr)
    {
        CookedArtifact cached = CacheCookStepArtifact(
            *probeArtifact, sourceRel, CookStepIds::IrradianceProbes,
            fingerprints.Probe, assetsRoot, transaction, &cookError);
        if (cached.Path.empty())
        {
            result.Error = "CookDocument: " + cookError;
            return false;
        }
        PutCookStepReceipt(receipt, CookStepReceipt{
            .StepId = std::string(CookStepIds::IrradianceProbes),
            .Version = FindDocumentCookStep(CookStepIds::IrradianceProbes)->Version,
            .InputFingerprint = fingerprints.Probe,
            .Dependencies = {
                { std::string(DocumentCookStepIds::OcclusionGeometry),
                  fingerprints.Occlusion },
            },
            .Artifacts = { std::move(cached) },
            .Metadata = DocumentCookResultMetadata(result),
        });
    }
    PutCookStepReceipt(receipt, CookStepReceipt{
        .StepId = std::string(DocumentCookStepIds::Publication),
        .Version = FindDocumentCookStep(DocumentCookStepIds::Publication)->Version,
        .InputFingerprint = documentHash,
        .Artifacts = std::move(catalog.Artifacts()),
        .Metadata = DocumentCookResultMetadata(result),
    });
    if (!SaveDocumentCookReceipt(transaction.Stage(paths.Receipt), receipt, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return false;
    }
    return true;
}
