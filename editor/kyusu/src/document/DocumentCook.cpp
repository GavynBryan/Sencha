#include "DocumentCook.h"
#include "DocumentCookInputData.h"

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
#include "DocumentPublication.h"
#include "EditorDocument.h"
#include "LightmapSurfaceCook.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/BrushClustering.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <assets/cook/TextureCook.h>
#include <core/assets/RuntimeAssets.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

DocumentCookResult ExecuteDocumentCook(DocumentCookInput input,
                                       std::string_view levelName,
                                       std::string_view sourceRel,
                                       const std::filesystem::path& assetsRoot,
                                       LoggingProvider& logging)
{
    DocumentCookResult result;
    if (input.Input == nullptr)
    {
        result.Error = "CookDocument: empty cook input";
        return result;
    }
    DocumentCookInput::Data& data = *input.Input;
    const DocumentCookRequest& request = data.Request;
    DocumentCookSnapshot& snapshot = data.Snapshot;
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
    CookChartSet& charts = snapshot.Charts;
    std::vector<CookBrushGeometry>& brushes = snapshot.Brushes;
    std::vector<LightmapPlacement>& placements = snapshot.Placements;
    CookArtifactTransaction transaction(assetsRoot, sourceRel);
    const DocumentBakeExtent bakeExtent = SelectDocumentBakeExtent(snapshot);

    const DocumentCookFingerprints fingerprints =
        ComputeDocumentCookFingerprints(snapshot, cellSize, bakeExtent.ReachableHalo);
    const std::uint64_t geometryHash = fingerprints.Document;
    const DocumentCookPaths paths = DeriveDocumentCookPaths(
        assetsRoot, sourceRel, levelName, request.OutputNamespace, geometryHash);
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

    if (!StageDocumentIndex(sourceRel, geometryHash, runReferencedAssets, runCollision,
                            std::move(pendingImports), catalog, paths, assetsRoot,
                            transaction, result))
        return result;

    result.CellCount = cells.size();
    if (!StageDocumentReceipt(sourceRel, geometryHash, profile, snapshot, fingerprints,
                              reuse, catalog, directLightmapArtifact,
                              ambientOcclusionArtifact, probeArtifact, paths, assetsRoot,
                              stemStr, hasCachedReceipt, cachedReceipt, transaction, result))
        return result;

    progress.Begin(DocumentCookStepIds::Publication);
    if (progress.Cancelled(result))
        return result;
    std::string cookError;
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
