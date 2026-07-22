#include "DocumentCook.h"
#include "DocumentCookInputData.h"

#include "BakeTriangleGather.h"
#include "BrushCookInput.h"
#include "CellArtifactCook.h"
#include "CookArtifactTransaction.h"
#include "CookGraph.h"
#include "CookReceipt.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "CookedSceneAssembly.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentBakeOcclusion.h"
#include "DocumentCookFingerprints.h"
#include "DocumentCookPaths.h"
#include "DocumentCookReuse.h"
#include "DocumentImportPublisher.h"
#include "DocumentLightmapBake.h"
#include "DocumentProbeBake.h"
#include "EditorDocument.h"
#include "LightmapSurfaceCook.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/BrushClustering.h>
#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/CollisionShapeCook.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <assets/cook/LightmapRaster.h>
#include <assets/cook/ProbeBake.h>
#include <assets/cook/SceneCookOutput.h>
#include <assets/cook/TextureCook.h>
#include <assets/probes/ProbeVolumeFormat.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <assets/texture/TextureSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/RuntimeAssets.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/MeshGeometry.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

JsonValue BuildCellEntity(const Vec3d& origin,
                          std::string_view meshPath,
                          std::span<const AssetRef> materials)
{
    JsonValue::Object local{
        { "position", JsonValue(JsonValue::Array{
            JsonValue(static_cast<double>(origin.X)),
            JsonValue(static_cast<double>(origin.Y)),
            JsonValue(static_cast<double>(origin.Z)) }) },
        { "rotation", JsonValue(JsonValue::Array{
            JsonValue(0.0), JsonValue(0.0), JsonValue(0.0), JsonValue(1.0) }) },
        { "scale", JsonValue(JsonValue::Array{
            JsonValue(1.0), JsonValue(1.0), JsonValue(1.0) }) },
    };

    JsonValue::Array materialPaths;
    materialPaths.reserve(materials.size());
    for (const AssetRef& material : materials)
        materialPaths.push_back(JsonValue(material.Path));

    JsonValue::Object staticMesh{
        { "mesh", JsonValue(std::string(meshPath)) },
        { "materials", JsonValue(std::move(materialPaths)) },
        { "visible", JsonValue(true) },
        { "layer_mask", JsonValue(static_cast<double>(0xFFFFFFFFu)) },
        { "section_mask", JsonValue(static_cast<double>(0xFFFFFFFFu)) },
    };

    return JsonValue(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "Transform", JsonValue(JsonValue::Object{ { "local", JsonValue(std::move(local)) } }) },
            { "StaticMesh", JsonValue(std::move(staticMesh)) },
        }) },
    });
}

namespace
{

DocumentCookResult CookDocumentKernel(DocumentCookInput::Data& input,
                                  std::string_view stem,
                                  std::string_view sourceRel,
                                  const std::filesystem::path& assetsRoot,
                                  LoggingProvider& logging)
{
    DocumentCookResult result;
    const DocumentCookRequest& request = input.Request;
    DocumentCookSnapshot& snapshot = input.Snapshot;
    const CookProfile& profile = request.Profile;
    result.ProfileId = profile.Id;
    CookStepProgress progress(request.Control, request.Graph.OrderedSteps);
    if (progress.Cancelled(result))
        return result;
    const bool runCollisionTarget = request.Selects(CookStepIds::Collision);
    const bool runReferencedAssets = request.Selects(CookStepIds::ReferencedAssets);
    const LightingCookParams& lightmapParams = snapshot.Lighting;
    const double cellSize = request.CellSize;
    std::vector<BakeDirectLight>& bakeLights = snapshot.BakeLights;
    std::vector<ProbeVolumeInput>& probeVolumes = snapshot.ProbeVolumes;
    std::vector<BakeDirectLight>& bounceLights = snapshot.BounceLights;
    CookChartSet& charts = snapshot.Charts;
    std::vector<CookBrushGeometry>& brushes = snapshot.Brushes;
    std::vector<LightmapPlacement>& placements = snapshot.Placements;
    const std::span<const ProbeHaloZone> halo = snapshot.Halo;
    CookArtifactTransaction transaction(assetsRoot, sourceRel);
    const DocumentBakeExtent bakeExtent = SelectDocumentBakeExtent(snapshot);

    const DocumentCookFingerprints fingerprints =
        ComputeDocumentCookFingerprints(snapshot, cellSize, bakeExtent.ReachableHalo);
    const std::uint64_t geometryHash = fingerprints.Document;
    const DocumentCookPaths paths = DeriveDocumentCookPaths(
        assetsRoot, sourceRel, stem, request.OutputNamespace, geometryHash);
    const std::string& stemStr = paths.Stem;

    // Referenced-asset freshness runs before the whole-document fast path: a
    // changed or missing referenced import produces a non-empty prepared set,
    // which defeats the cache hit so the import re-publishes this cook. The
    // prepared bytes publish through the transaction below (or are discarded on
    // a hit). The scan is cheap when every source is fresh (stat checks).
    PendingAssetImport pendingImports;
    if (runReferencedAssets)
    {
        progress.Begin(CookStepIds::ReferencedAssets);
        PngTextureImporter textureImporter;
        AssetImporterRegistry importers;
        importers.Register(textureImporter);
        (void)PrepareAssetsOnDemand(assetsRoot, importers, logging, pendingImports);
        progress.Complete();
    }
    const bool referencedAssetsFresh = pendingImports.Artifacts.empty();

    DocumentCookReceipt cachedReceipt;
    const bool hasCachedReceipt = !request.ForceRebuild
        && LoadDocumentCookReceipt(paths.Receipt, cachedReceipt);
    std::error_code cacheEc;
    if (std::optional<DocumentCookResult> hit = TryDocumentCookCacheHit(
            request, paths, assetsRoot, sourceRel, geometryHash,
            referencedAssetsFresh, hasCachedReceipt, cachedReceipt, progress))
        return *hit;

    const DocumentCookReceipt* priorReceipt = hasCachedReceipt ? &cachedReceipt : nullptr;
    const DocumentCookReuse reuse =
        ResolveDocumentCookReuse(priorReceipt, assetsRoot, snapshot, fingerprints);
    progress.Begin(DocumentCookStepIds::BrushCells);
    std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, cellSize);
    progress.Complete();
    if (progress.Cancelled(result))
        return result;
    const bool runCollision = runCollisionTarget
        || !std::filesystem::exists(paths.Collision, cacheEc);

    // Pack the atlas and write final atlas UVs into the cell vertices BEFORE
    // the per-cell mesh bake: the weld compares whole vertices, so identical
    // UVs weld chart interiors and differing UVs split chart borders, with no
    // dedicated chart-splitting logic.
    LightmapAtlasLayout atlasLayout;
    if (!bakeLights.empty())
    {
        progress.Begin(DocumentCookStepIds::LightmapSurfaces);
        std::string surfaceError;
        if (!LayoutLightmapSurfaces(cells, placements, charts, lightmapParams,
                                    snapshot.PassthroughScene, logging, atlasLayout,
                                    &surfaceError))
        {
            result.Error = "CookDocument: " + surfaceError;
            return result;
        }
        progress.Complete();
        if (progress.Cancelled(result))
            return result;
    }

    JsonValue::Array cellEntities;
    DocumentArtifactCatalog catalog;
    std::vector<CellCollisionEntry> collisionEntries;
    std::optional<CookedArtifact> directLightmapArtifact;
    std::optional<CookedArtifact> ambientOcclusionArtifact;
    std::optional<CookedArtifact> probeArtifact;
    std::vector<PendingCellMesh> pendingMeshes;

    if (!EmitCellArtifacts(cells, assetsRoot, stemStr, runCollision, transaction,
                           catalog, progress, pendingMeshes, cellEntities,
                           collisionEntries, result))
        return result;

    // One occlusion BVH over every cell's world triangles serves both bakes:
    // a light in one cell shadows onto its neighbors, and probe rays see the
    // whole document plus any neighbor-zone halo within reach.
    BakeBvh occlusionBvh;
    const bool needsOcclusion = (!bakeLights.empty() && !reuse.ReuseLighting)
        || (!probeVolumes.empty() && reuse.Probes == nullptr);
    if (needsOcclusion)
    {
        progress.Begin(DocumentCookStepIds::OcclusionGeometry);
        occlusionBvh = BuildDocumentOcclusionBvh(pendingMeshes, snapshot, bakeExtent);
        progress.Complete();
        if (progress.Cancelled(result))
            return result;
    }

    // Bake static direct lighting into the zone's lightmap atlas. The lights
    // were collected up front (folded into the cook hash).
    if (!bakeLights.empty())
    {
        if (!BakeDocumentLightmap(snapshot, cells, atlasLayout, occlusionBvh, reuse,
                                  assetsRoot, stemStr, transaction, catalog, progress,
                                  logging, cellEntities, directLightmapArtifact,
                                  ambientOcclusionArtifact, result))
            return result;
    }

    // Bake authored irradiance-probe volumes into the zone's .sprobe. Bounce
    // comes from Direct and Indirect lights against the same occlusion BVH;
    // the runtime locates the file by the cooked-scene path convention.
    if (!probeVolumes.empty())
    {
        if (!BakeDocumentProbes(snapshot, occlusionBvh, reuse, assetsRoot, stemStr,
                                transaction, catalog, progress, probeArtifact, result))
            return result;
    }

    if (!WriteCookedSceneArtifacts(std::move(snapshot.PassthroughScene), pendingMeshes,
                                   cellEntities, collisionEntries, runCollision, catalog,
                                   paths, assetsRoot, transaction, progress, logging,
                                   result))
        return result;

    std::string cookError;

    // Referenced-asset imports were prepared before the fast path; publish their
    // staged bytes and index deltas through the document transaction here so
    // they commit atomically with the rest of the publication.

    // Record source -> artifacts (source key = caller-supplied rel path, hash key
    // = brush-geometry hash). The index loads active (prepare left it untouched);
    // the prepared imports, their index deltas, and the level entry then publish
    // through this one staged index and the transaction, committing together.
    CookedCacheIndex index;
    (void)CookedCacheIndex::LoadFromFile(paths.Index.generic_string(), index); // cold cache is fine
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
                return result;
            }
        DocumentImportPublisher publisher(transaction, assetsRoot, index);
        if (!PublishAssetImport(std::move(pendingImports), publisher, &cookError))
        {
            result.Error = "CookDocument: " + cookError;
            return result;
        }
    }
    if (!runCollision)
    {
        if (const CookedSourceEntry* previous = index.Find(sourceRel))
            for (const CookedArtifact& artifact : previous->Artifacts)
                if (artifact.Type == AssetType::Collision)
                    catalog.AddExisting(artifact);
    }
    CookedSourceEntry entry;
    entry.SourceRelPath = std::string(sourceRel);
    entry.InputFingerprint = geometryHash;
    for (CookedArtifact& artifact : catalog.Artifacts())
    {
        const std::filesystem::path active = assetsRoot / artifact.FileRelPath;
        const std::filesystem::path contentPath =
            !runCollision && artifact.Type == AssetType::Collision
            ? active
            : transaction.Stage(active);
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
        return result;
    }

    DocumentCookReceipt receipt = hasCachedReceipt
        ? cachedReceipt : DocumentCookReceipt{};
    result.CellCount = cells.size();
    receipt.Target = std::string(sourceRel);
    receipt.PublishedProfileId = profile.Id;
    receipt.PublishedFingerprint = geometryHash;
    receipt.PublishedOutputFamilies = {
        std::string(CookOutputFamilies::Structure),
    };
    if (CookProfileOutputDisposition(profile, CookOutputFamilies::Collision)
        != CookOutputDisposition::Withdraw)
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::Collision));
    if (CookProfileOutputDisposition(profile, CookOutputFamilies::ReferencedAssets)
        != CookOutputDisposition::Withdraw)
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::ReferencedAssets));
    if (!bakeLights.empty())
    {
        receipt.PublishedOutputFamilies.push_back(
            std::string(CookOutputFamilies::DirectLightmap));
        if (lightmapParams.Ao.Enabled)
            receipt.PublishedOutputFamilies.push_back(
                std::string(CookOutputFamilies::AmbientOcclusion));
    }
    if (!probeVolumes.empty())
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

    if (directLightmapArtifact.has_value() && !reuse.ReuseLighting)
    {
        CookedArtifact cached = CacheCookStepArtifact(
            *directLightmapArtifact, sourceRel, CookStepIds::DirectLightmap,
            fingerprints.Direct, assetsRoot, transaction, &cookError);
        if (cached.Path.empty())
        {
            result.Error = "CookDocument: " + cookError;
            return result;
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
    if (ambientOcclusionArtifact.has_value() && !reuse.ReuseLighting)
    {
        CookedArtifact cached = CacheCookStepArtifact(
            *ambientOcclusionArtifact, sourceRel, CookStepIds::AmbientOcclusion,
            fingerprints.Ao, assetsRoot, transaction, &cookError);
        if (cached.Path.empty())
        {
            result.Error = "CookDocument: " + cookError;
            return result;
        }
        PutCookStepReceipt(receipt, CookStepReceipt{
            .StepId = std::string(CookStepIds::AmbientOcclusion),
            .Version = FindDocumentCookStep(CookStepIds::AmbientOcclusion)->Version,
            .InputFingerprint = fingerprints.Ao,
            .Dependencies = {
                { std::string(CookStepIds::DirectLightmap), fingerprints.Direct },
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
            return result;
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
        .InputFingerprint = geometryHash,
        .Artifacts = std::move(catalog.Artifacts()),
        .Metadata = DocumentCookResultMetadata(result),
    });
    if (!SaveDocumentCookReceipt(transaction.Stage(paths.Receipt), receipt, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }
    progress.Begin(DocumentCookStepIds::Publication);
    if (progress.Cancelled(result))
        return result;
    if (!transaction.Commit(&cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }
    progress.Complete();

    result.Success = true;
    result.CookedScenePath = paths.Scene;
    result.ManifestPath = paths.Manifest;
    result.CollisionSidecarPath = paths.Collision;
    result.ContentHash = geometryHash;
    result.CellCount = cells.size();
    progress.Finish();
    return result;
}
} // namespace

DocumentCookResult ExecuteDocumentCook(DocumentCookInput input,
                                       std::string_view levelName,
                                       std::string_view sourceRel,
                                       const std::filesystem::path& assetsRoot,
                                       LoggingProvider& logging)
{
    if (input.Input == nullptr)
    {
        DocumentCookResult result;
        result.Error = "CookDocument: empty cook input";
        return result;
    }
    return CookDocumentKernel(*input.Input, levelName, sourceRel, assetsRoot, logging);
}

DocumentCookResult CookDocument(const std::filesystem::path& authoredLevelPath,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider* logging,
                          RuntimeAssets* assets,
                          const LightingCookParams& lightmapParams,
                          std::span<const ProbeHaloZone> halo,
                          const DocumentCookOptions& options)
{
    // A sink-less local logger keeps the no-logging call headless and silent.
    LoggingProvider silent;
    LoggingProvider& log = logging != nullptr ? *logging : silent;
    EditorDocument doc(log);
    if (assets != nullptr)
        doc.SetAssetEnvironment(*assets);
    if (!doc.Load(authoredLevelPath.generic_string()))
    {
        DocumentCookResult result;
        result.Error = "CookDocument: could not load '" + authoredLevelPath.generic_string() + "'";
        return result;
    }

    std::error_code ec;
    const std::string sourceRel =
        std::filesystem::relative(authoredLevelPath, assetsRoot, ec).generic_string();
    std::string inputError;
    std::optional<DocumentCookInput> input = CollectDocumentCookInput(
        doc, assetsRoot, cellSize, log, assets, lightmapParams, halo, options,
        &inputError);
    if (!input.has_value())
    {
        DocumentCookResult result;
        result.Error = "CookDocument: " + inputError;
        return result;
    }
    return ExecuteDocumentCook(std::move(*input),
                               authoredLevelPath.stem().generic_string(), sourceRel,
                               assetsRoot, log);
}

DocumentCookResult CookDocument(const EditorDocument& liveDocument,
                          std::string_view levelName,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider& logging,
                          RuntimeAssets* assets,
                          const LightingCookParams& lightmapParams,
                          const DocumentCookOptions& options)
{
    std::string inputError;
    std::optional<DocumentCookInput> input = CollectDocumentCookInput(
        liveDocument, assetsRoot, cellSize, logging, assets, lightmapParams, {},
        options, &inputError);
    if (!input.has_value())
    {
        DocumentCookResult result;
        result.Error = "CookDocument: " + inputError;
        return result;
    }

    const std::string sourceRel = "levels/" + std::string(levelName) + ".level.json";
    return ExecuteDocumentCook(std::move(*input), levelName, sourceRel,
                               assetsRoot, logging);
}
