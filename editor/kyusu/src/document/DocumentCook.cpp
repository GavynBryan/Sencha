#include "DocumentCook.h"
#include "DocumentCookInputData.h"

#include "BrushCookInput.h"
#include "CookArtifactTransaction.h"
#include "CookGraph.h"
#include "CookReceipt.h"
#include "CookStepCache.h"
#include "DocumentCookFingerprints.h"
#include "DocumentImportPublisher.h"
#include "EditorDocument.h"

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
#include <format>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    // Unorm16 lightmap UV component (texel-center convention).
    std::uint16_t PackLightmapUv16(float value)
    {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
    }

    std::string CellBase(const Vec3i& coord)
    {
        return "cell_" + std::to_string(coord.X) + "_" + std::to_string(coord.Y)
            + "_" + std::to_string(coord.Z);
    }

    std::string CellName(const Vec3i& coord) { return CellBase(coord) + ".smesh"; }

    // Flatten a cell's already-triangulated faces into a position/index soup for
    // the collision bake (cell-local, the same triangles the render mesh uses).
    void CollectCellTriangles(const std::vector<CookFace>& faces,
                              std::vector<Vec3d>& positions,
                              std::vector<uint32_t>& indices)
    {
        for (const CookFace& face : faces)
            for (const StaticMeshVertex& vertex : face.Triangles)
            {
                indices.push_back(static_cast<uint32_t>(positions.size()));
                positions.push_back(vertex.Position);
            }
    }

    // One cell's cooked collision: the blob's path (relative to the cooked root)
    // and the cell origin the runtime places the static collider at.
    struct CollisionEntry
    {
        std::string BlobRelPath;
        Vec3d Origin;
    };

} // namespace

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
JsonValue* FindMutable(JsonValue& value, std::string_view key)
{
    if (!value.IsObject())
        return nullptr;
    for (auto& [field, child] : value.AsObject())
        if (field == key)
            return &child;
    return nullptr;
}

bool SetLightmapScaleBias(JsonValue& scene, std::uint32_t entityIndex,
                          const Vec4& value)
{
    JsonValue* entities = FindMutable(scene, "entities");
    if (entities == nullptr || !entities->IsArray()
        || entityIndex >= entities->AsArray().size())
        return false;
    JsonValue* components = FindMutable(entities->AsArray()[entityIndex], "components");
    if (components == nullptr || !components->IsObject())
        return false;
    JsonValue* mesh = FindMutable(*components, "StaticMesh");
    if (mesh == nullptr || !mesh->IsObject())
        return false;

    JsonValue scaleBias(JsonValue::Array{
        JsonValue(value.X), JsonValue(value.Y), JsonValue(value.Z), JsonValue(value.W)
    });
    if (JsonValue* existing = FindMutable(*mesh, "lightmap_scale_bias"))
        *existing = std::move(scaleBias);
    else
        mesh->AsObject().emplace_back("lightmap_scale_bias", std::move(scaleBias));
    return true;
}

} // namespace

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
    const std::shared_ptr<CookControl>& control = request.Control;
    if (control != nullptr)
    {
        control->TotalSteps.store(static_cast<std::uint32_t>(request.Graph.OrderedSteps.size()));
        control->CompletedSteps.store(0);
        control->CurrentStep.store(0);
    }
    const auto beginStep = [&request, &control](std::string_view step)
    {
        if (control == nullptr)
            return;
        const auto found = std::find(request.Graph.OrderedSteps.begin(),
                                     request.Graph.OrderedSteps.end(), step);
        if (found != request.Graph.OrderedSteps.end())
            control->CurrentStep.store(static_cast<std::uint32_t>(
                std::distance(request.Graph.OrderedSteps.begin(), found)));
    };
    const auto completeStep = [&control]
    {
        if (control != nullptr)
            control->CompletedSteps.fetch_add(1);
    };
    const auto cancelled = [&control, &result]
    {
        if (control == nullptr || !control->IsCancellationRequested())
            return false;
        result.Cancelled = true;
        result.Error = "CookDocument: cancelled";
        return true;
    };
    if (cancelled())
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
    // Neighbor-zone halo: read-only occluder geometry other zones offered.
    // Only zones within probe-ray reach of this zone's bake inputs
    // participate; the selected set folds into the cook hash, so a neighbor
    // edit within reach restales this zone and one beyond reach does not. The
    // occlusion BVH below consumes the same reachable set.
    std::vector<const ProbeHaloZone*> reachableHalo;
    Aabb3d bakeBounds = Aabb3d::Empty();
    if ((!bakeLights.empty() || !probeVolumes.empty()) && !halo.empty())
    {
        for (const CookBrushGeometry& brush : brushes)
            bakeBounds.ExpandToInclude(brush.WorldBounds);
        for (const LightmapPlacement& placement : placements)
            if (placement.CastsIntoBake)
                for (const StaticMeshVertex& vertex : placement.Geometry.Vertices)
                    bakeBounds.ExpandToInclude(
                        placement.ToWorld.TransformPoint(vertex.Position));
        for (const ProbeVolumeInput& volume : probeVolumes)
            bakeBounds.ExpandToInclude(volume.Grid.Bounds());
        reachableHalo = SelectProbeHaloZones(bakeBounds, halo,
                                             lightmapParams.Probe.MaxRayDistance);
    }

    const DocumentCookFingerprints fingerprints =
        ComputeDocumentCookFingerprints(snapshot, cellSize, reachableHalo);
    const std::uint64_t geometryHash = fingerprints.Document;
    const std::string stemStr = request.OutputNamespace.empty()
        ? std::string(stem)
        : request.OutputNamespace + "/" + std::format("{:016x}", geometryHash);

    const std::filesystem::path activeCookedDir = assetsRoot / ".cooked/levels";
    const std::filesystem::path activeScene = activeCookedDir / (stemStr + ".cooked.json");
    const std::filesystem::path activeManifest = activeCookedDir / (stemStr + ".manifest.json");
    const std::filesystem::path activeCollision = activeCookedDir / (stemStr + ".collision.json");
    const std::filesystem::path activeReceipt =
        DocumentCookReceiptPath(assetsRoot, sourceRel);
    const std::filesystem::path activeIndex = assetsRoot / ".cooked/index.json";
    CookedCacheIndex cachedIndex;
    DocumentCookReceipt cachedReceipt;
    const bool hasCachedReceipt = !request.ForceRebuild
        && LoadDocumentCookReceipt(activeReceipt, cachedReceipt);
    std::error_code cacheEc;
    if (!request.ForceRebuild
        && CookedCacheIndex::LoadFromFile(activeIndex.generic_string(), cachedIndex)
        && hasCachedReceipt
        && cachedReceipt.PublishedProfileId == profile.Id
        && cachedReceipt.PublishedFingerprint == geometryHash
        && std::filesystem::exists(activeScene, cacheEc)
        && std::filesystem::exists(activeManifest, cacheEc)
        && std::filesystem::exists(activeCollision, cacheEc))
    {
        const CookedSourceEntry* cached = cachedIndex.Find(sourceRel);
        if (cached != nullptr && cached->InputFingerprint == geometryHash
            && CookedSourceArtifactsMatch(assetsRoot, *cached))
        {
            result.Success = true;
            result.CacheHit = true;
            result.ReusedSteps = request.Graph.OrderedSteps;
            result.ProfileId = profile.Id;
            result.ContentHash = geometryHash;
            result.CookedScenePath = activeScene;
            result.ManifestPath = activeManifest;
            result.CollisionSidecarPath = activeCollision;
            if (const CookStepReceipt* publication =
                    FindCookStepReceipt(cachedReceipt, DocumentCookStepIds::Publication))
                RestoreDocumentCookResultMetadata(publication->Metadata, result);
            for (const CookedArtifact& artifact : cached->Artifacts)
                if (artifact.Type == AssetType::StaticMesh)
                    result.GeneratedMeshPaths.push_back(artifact.Path);
            if (control != nullptr)
                control->CompletedSteps.store(control->TotalSteps.load());
            return result;
        }
    }

    const DocumentCookReceipt* priorReceipt = hasCachedReceipt ? &cachedReceipt : nullptr;
    const CookStepReceipt* reusableDirect = !bakeLights.empty()
        ? FindReusableCookStep(priorReceipt, assetsRoot, CookStepIds::DirectLightmap,
                       fingerprints.Direct)
        : nullptr;
    const CookStepReceipt* reusableAo = lightmapParams.Ao.Enabled
        ? FindReusableCookStep(priorReceipt, assetsRoot, CookStepIds::AmbientOcclusion,
                       fingerprints.Ao)
        : nullptr;
    const bool reuseLighting = reusableDirect != nullptr
        && (!lightmapParams.Ao.Enabled || reusableAo != nullptr);
    const CookStepReceipt* reusableProbes = !probeVolumes.empty()
        ? FindReusableCookStep(priorReceipt, assetsRoot, CookStepIds::IrradianceProbes,
                       fingerprints.Probe)
        : nullptr;
    beginStep(DocumentCookStepIds::BrushCells);
    std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, cellSize);
    completeStep();
    if (cancelled())
        return result;
    const bool runCollision = runCollisionTarget
        || !std::filesystem::exists(activeCollision, cacheEc);

    // Pack the atlas and write final atlas UVs into the cell vertices BEFORE
    // the per-cell mesh bake: the weld compares whole vertices, so identical
    // UVs weld chart interiors and differing UVs split chart borders, with no
    // dedicated chart-splitting logic.
    LightmapAtlasLayout atlasLayout;
    if (!bakeLights.empty())
    {
        beginStep(DocumentCookStepIds::LightmapSurfaces);
        // Placements pack into the same zone atlas as the brush charts: one
        // rect per placement, sized by the world span its [0,1] sheet covers.
        std::vector<Vec2d> extents = charts.Extents;
        for (LightmapPlacement& placement : placements)
        {
            placement.Chart = static_cast<std::uint32_t>(extents.size());
            extents.push_back(placement.WorldExtent);
        }
        atlasLayout = PackLightmapAtlas(
            extents, lightmapParams.LuxelSize, lightmapParams.MaxAtlasSize);
        const float luxel = atlasLayout.EffectiveLuxelSize;

        // Per-placement scale/bias: the mesh's sheet UVs map linearly into
        // its rect's grid points; the runtime applies uv * xy + zw. Written
        // into the cook document's component so SaveSceneJson serializes it.
        for (const LightmapPlacement& placement : placements)
        {
            const LightmapChartRect& rect = atlasLayout.Rects[placement.Chart];
            const float pointsU = std::ceil(placement.WorldExtent.X / luxel);
            const float pointsV = std::ceil(placement.WorldExtent.Y / luxel);
            const Vec4 scaleBias{
                pointsU / static_cast<float>(atlasLayout.Width),
                pointsV / static_cast<float>(atlasLayout.Height),
                (static_cast<float>(rect.X + kLightmapGutter) + 0.5f)
                    / static_cast<float>(atlasLayout.Width),
                (static_cast<float>(rect.Y + kLightmapGutter) + 0.5f)
                    / static_cast<float>(atlasLayout.Height) };
            if (!SetLightmapScaleBias(snapshot.PassthroughScene,
                                      placement.SceneEntityIndex, scaleBias))
            {
                result.Error = "CookDocument: could not apply placement lightmap layout";
                return result;
            }
        }
        for (BrushCell& cell : cells)
            for (CookFace& face : cell.Faces)
            {
                if (face.Chart >= atlasLayout.Rects.size())
                    continue;
                const LightmapChartRect& rect = atlasLayout.Rects[face.Chart];
                for (std::size_t i = 0; i < face.Triangles.size()
                     && i < face.ChartUv.size(); ++i)
                {
                    // Grid point k maps to the CENTER of texel (rect + gutter
                    // + k); bilinear filtering then interpolates exactly
                    // between adjacent grid samples.
                    const float u = (static_cast<float>(rect.X + kLightmapGutter)
                        + face.ChartUv[i].X / luxel + 0.5f)
                        / static_cast<float>(atlasLayout.Width);
                    const float v = (static_cast<float>(rect.Y + kLightmapGutter)
                        + face.ChartUv[i].Y / luxel + 0.5f)
                        / static_cast<float>(atlasLayout.Height);
                    face.Triangles[i].LightmapU = PackLightmapUv16(u);
                    face.Triangles[i].LightmapV = PackLightmapUv16(v);
                }
            }
        if (luxel != lightmapParams.LuxelSize)
            logging.GetLogger<EditorDocument>().Warn(
                "cook: lightmap atlas overflowed {}x{}; luxel size clamped {} -> {}",
                lightmapParams.MaxAtlasSize, lightmapParams.MaxAtlasSize,
                lightmapParams.LuxelSize, luxel);
        completeStep();
        if (cancelled())
            return result;
    }

    MeshSerializer serializer(logging);

    JsonValue::Array cellEntities;
    std::vector<CookedArtifact> artifacts;
    std::vector<std::string> materialRefs; // distinct face materials, in cook order
    std::unordered_set<std::string> seenMaterial;
    std::unordered_set<std::string> generatedMeshPaths;
    std::vector<CollisionEntry> collisionEntries;
    std::optional<CookedArtifact> directLightmapArtifact;
    std::optional<CookedArtifact> ambientOcclusionArtifact;
    std::optional<CookedArtifact> probeArtifact;

    // Cell meshes are written after the lighting bake, not inline: the bake
    // needs every cell's geometry (for the shared occlusion BVH) before any
    // cell's channel is final. Origin is the cell's world translation.
    struct PendingCellMesh
    {
        std::filesystem::path Physical;
        MeshGeometry Geometry;
        Vec3d Origin;
    };
    std::vector<PendingCellMesh> pendingMeshes;

    beginStep(CookStepIds::RenderMeshes);
    for (const BrushCell& cell : cells)
    {
        if (cancelled())
            return result;
        std::vector<AssetRef> order = CollectMaterialOrder(cell.Faces);

        MeshGeometry geometry;
        std::string bakeError;
        if (!BakeBrushFacesToStaticMesh(cell.Faces, order, geometry, &bakeError))
        {
            result.Error = "CookDocument: " + bakeError;
            return result;
        }

        const std::string cellName = CellName(cell.Coord);
        const std::string meshAssetPath = "asset://levels/" + stemStr + "/" + cellName;
        const std::string meshRelPath = ".cooked/levels/" + stemStr + "/" + cellName;
        const std::filesystem::path meshPhysical = assetsRoot / meshRelPath;
        const std::filesystem::path meshStaged = transaction.Stage(meshPhysical);

        std::error_code ec;
        std::filesystem::create_directories(meshStaged.parent_path(), ec);
        pendingMeshes.push_back(PendingCellMesh{
            meshStaged, std::move(geometry), cell.Origin });

        cellEntities.push_back(BuildCellEntity(cell.Origin, meshAssetPath, order));
        artifacts.push_back(CookedArtifact{ meshAssetPath, meshRelPath, AssetType::StaticMesh });
        result.GeneratedMeshPaths.push_back(meshAssetPath);
        generatedMeshPaths.insert(meshAssetPath);
        for (const AssetRef& material : order)
            if (seenMaterial.insert(material.Path).second)
                materialRefs.push_back(material.Path);

        // Collision: bake the same cell triangles into a pre-baked Jolt blob, a
        // sibling of the cell mesh. Authored brushes become collidable with no
        // collider authoring; the runtime loads these from the sidecar at map load.
        if (runCollision)
        {
            std::vector<Vec3d> collisionPositions;
            std::vector<uint32_t> collisionIndices;
            CollectCellTriangles(cell.Faces, collisionPositions, collisionIndices);
            const std::vector<std::byte> collisionBlob =
                BakeCollisionBlob(collisionPositions, collisionIndices);
            if (!collisionBlob.empty())
            {
                const std::string colRel =
                    "levels/" + stemStr + "/" + CellBase(cell.Coord) + ".scol";
                const std::filesystem::path colPhysical = assetsRoot / ".cooked" / colRel;
                const std::filesystem::path colStaged = transaction.Stage(colPhysical);
                std::ofstream colFile(colStaged, std::ios::binary);
                colFile.write(reinterpret_cast<const char*>(collisionBlob.data()),
                              static_cast<std::streamsize>(collisionBlob.size()));
                if (colFile.good())
                {
                    collisionEntries.push_back(CollisionEntry{ colRel, cell.Origin });
                    artifacts.push_back(CookedArtifact{
                        "asset://" + colRel, ".cooked/" + colRel,
                        AssetType::Collision });
                }
            }
        }
    }
    completeStep();
    if (runCollision)
        completeStep();

    // One occlusion BVH over every cell's world triangles serves both bakes:
    // a light in one cell shadows onto its neighbors, and probe rays see the
    // whole document plus any neighbor-zone halo within reach.
    BakeBvh occlusionBvh;
    const bool needsOcclusion = (!bakeLights.empty() && !reuseLighting)
        || (!probeVolumes.empty() && reusableProbes == nullptr);
    if (needsOcclusion)
    {
        beginStep(DocumentCookStepIds::OcclusionGeometry);
        std::vector<BakeTriangle> occluders;
        for (const PendingCellMesh& pending : pendingMeshes)
        {
            const Mat4 toWorld = Mat4::MakeTranslation(pending.Origin);
            const MeshGeometry& geometry = pending.Geometry;
            for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            {
                occluders.push_back(BakeTriangle{
                    toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i]].Position),
                    toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i + 1]].Position),
                    toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i + 2]].Position) });
            }
        }
        // Placed meshes occlude too (and shadow each other) unless authored
        // out via AffectsBakedLighting.
        for (const LightmapPlacement& placement : placements)
        {
            if (!placement.CastsIntoBake)
                continue;
            const MeshGeometry& geometry = placement.Geometry;
            for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            {
                occluders.push_back(BakeTriangle{
                    placement.ToWorld.TransformPoint(
                        geometry.Vertices[geometry.Indices[i]].Position),
                    placement.ToWorld.TransformPoint(
                        geometry.Vertices[geometry.Indices[i + 1]].Position),
                    placement.ToWorld.TransformPoint(
                        geometry.Vertices[geometry.Indices[i + 2]].Position) });
            }
        }
        occlusionBvh.Build(AssembleProbeBakeTriangles(
            std::move(occluders), bakeBounds, halo,
            lightmapParams.Probe.MaxRayDistance));
        completeStep();
        if (cancelled())
            return result;
    }

    // Bake static direct lighting into the zone's lightmap atlas. The lights
    // were collected up front (folded into the cook hash).
    if (!bakeLights.empty())
    {
        result.DirectLightCount = bakeLights.size();
        result.LightmapAtlasWidth = atlasLayout.Width;
        result.LightmapAtlasHeight = atlasLayout.Height;
        result.EffectiveLuxelSize = atlasLayout.EffectiveLuxelSize;
        if (reuseLighting)
        {
            std::string restoreError;
            beginStep(CookStepIds::DirectLightmap);
            if (!RestoreCookStepArtifacts(*reusableDirect, assetsRoot, transaction,
                                      artifacts, generatedMeshPaths, materialRefs,
                                      true, &restoreError))
            {
                result.Error = "CookDocument: " + restoreError;
                return result;
            }
            directLightmapArtifact = artifacts.back();
            result.ReusedSteps.push_back(std::string(CookStepIds::DirectLightmap));
            RestoreDocumentCookResultMetadata(reusableDirect->Metadata, result);
            completeStep();

            JsonValue::Object lightmapFields{
                { "texture", JsonValue(directLightmapArtifact->Path) },
            };
            if (lightmapParams.Ao.Enabled)
            {
                beginStep(CookStepIds::AmbientOcclusion);
                if (!RestoreCookStepArtifacts(*reusableAo, assetsRoot, transaction,
                                          artifacts, generatedMeshPaths, materialRefs,
                                          true, &restoreError))
                {
                    result.Error = "CookDocument: " + restoreError;
                    return result;
                }
                ambientOcclusionArtifact = artifacts.back();
                result.ReusedSteps.push_back(
                    std::string(CookStepIds::AmbientOcclusion));
                lightmapFields.push_back({
                    "ao", JsonValue(ambientOcclusionArtifact->Path) });
                completeStep();
            }
            cellEntities.push_back(JsonValue(JsonValue::Object{
                { "components", JsonValue(JsonValue::Object{
                    { "ZoneLightmap", JsonValue(std::move(lightmapFields)) },
                }) },
            }));
        }
        else
        {
        beginStep(CookStepIds::DirectLightmap);

        // Gather each chart's triangles (world positions + smoothed normals +
        // chart grid UVs) from the pre-weld cell faces, then rasterize and
        // bake chart by chart. Each chart's span holds only its own surface:
        // a boundary luxel bakes the one-sided limit of its own chart, so an
        // illumination step that lies exactly on a chart seam (a wall base, a
        // shadow plane through a light) stays crisp instead of averaging both
        // sides into the shared boundary texels.
        std::vector<std::vector<LightmapRasterTriangle>> chartTriangles(
            atlasLayout.Rects.size());
        const float luxel = atlasLayout.EffectiveLuxelSize;
        for (const BrushCell& cell : cells)
            for (const CookFace& face : cell.Faces)
            {
                if (face.Chart >= chartTriangles.size())
                    continue;
                for (std::size_t i = 0; i + 2 < face.Triangles.size()
                     && i + 2 < face.ChartUv.size(); i += 3)
                {
                    LightmapRasterTriangle tri;
                    for (int k = 0; k < 3; ++k)
                    {
                        tri.Uv[k] = Vec2d{ face.ChartUv[i + k].X / luxel,
                                           face.ChartUv[i + k].Y / luxel };
                        tri.Position[k] = face.Triangles[i + k].Position + cell.Origin;
                        tri.Normal[k] = face.Triangles[i + k].Normal;
                    }
                    chartTriangles[face.Chart].push_back(tri);
                }
            }

        // Placement charts: sheet UVs scale into the rect's grid points; the
        // luxel bake then runs on the placed world triangles like any chart.
        for (const LightmapPlacement& placement : placements)
        {
            const float pointsU = std::ceil(placement.WorldExtent.X / luxel);
            const float pointsV = std::ceil(placement.WorldExtent.Y / luxel);
            const MeshGeometry& geometry = placement.Geometry;
            if (placement.Chart >= chartTriangles.size())
                continue;
            std::vector<LightmapRasterTriangle>& out = chartTriangles[placement.Chart];
            for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            {
                LightmapRasterTriangle tri;
                for (int k = 0; k < 3; ++k)
                {
                    const StaticMeshVertex& vertex = geometry.Vertices[geometry.Indices[i + k]];
                    tri.Uv[k] = Vec2d{ vertex.LightmapU / 65535.0f * pointsU,
                                       vertex.LightmapV / 65535.0f * pointsV };
                    tri.Position[k] = placement.ToWorld.TransformPoint(vertex.Position);
                    tri.Normal[k] =
                        placement.ToWorld.TransformVector(vertex.Normal).Normalized();
                }
                out.push_back(tri);
            }
        }

        std::vector<std::uint32_t> atlasPixels(
            static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height, 0u);
        // The AO plane initializes white: texels no chart covers (including
        // the reserved border and the (0, 0) texel unbaked items sample)
        // must never darken the ambient term.
        const bool bakeAo = lightmapParams.Ao.Enabled;
        std::vector<std::uint8_t> aoPixels;
        std::vector<Vec3d> aoRayTable;
        AmbientOcclusionBakeParams aoBake;
        if (bakeAo)
        {
            aoPixels.assign(
                static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height,
                255u);
            aoRayTable = BuildProbeRayTable(lightmapParams.Ao.RayCount);
            aoBake.MaxDistance = lightmapParams.Ao.MaxDistance;
            aoBake.NormalOffset = lightmapParams.Shading.NormalOffset;
            aoBake.RayTable = aoRayTable;
        }
        for (std::size_t c = 0; c < chartTriangles.size(); ++c)
        {
            if (cancelled())
                return result;
            BakeChartLuxels(chartTriangles[c], atlasLayout.Rects[c], bakeLights,
                            occlusionBvh, lightmapParams.Shading,
                            atlasLayout.Width, atlasPixels,
                            bakeAo ? &aoBake : nullptr, aoPixels);
        }

        // Emit the atlas as a cooked texture artifact plus the zone component
        // that binds it at runtime.
        TextureData atlas;
        atlas.Format = TexturePixelFormat::RGB9E5;
        atlas.Usage = TextureUsage::LinearData;
        atlas.Filter = TextureFilter::Linear;
        atlas.Width = atlasLayout.Width;
        atlas.Height = atlasLayout.Height;
        atlas.Mips = { TextureMipLevel{ atlasLayout.Width, atlasLayout.Height, 0,
                                        atlasPixels.size() * sizeof(std::uint32_t) } };
        atlas.Blob.resize(atlasPixels.size() * sizeof(std::uint32_t));
        std::memcpy(atlas.Blob.data(), atlasPixels.data(), atlas.Blob.size());

        const std::string atlasRel = "levels/" + stemStr + "/lightmap.stex";
        const std::string atlasAssetPath = "asset://" + atlasRel;
        TextureSerializer textureSerializer(logging);
        const std::filesystem::path atlasPhysical = assetsRoot / ".cooked" / atlasRel;
        if (!textureSerializer.WriteToFile(
                transaction.Stage(atlasPhysical).generic_string(), atlas))
        {
            result.Error = "CookDocument: could not write lightmap atlas '" + atlasRel + "'";
            return result;
        }
        artifacts.push_back(CookedArtifact{
            atlasAssetPath, ".cooked/" + atlasRel, AssetType::Texture });
        directLightmapArtifact = artifacts.back();
        generatedMeshPaths.insert(atlasAssetPath);
        materialRefs.push_back(atlasAssetPath);

        JsonValue::Object lightmapFields{
            { "texture", JsonValue(atlasAssetPath) },
        };
        if (bakeAo)
        {
            TextureData aoAtlas;
            aoAtlas.Format = TexturePixelFormat::R8;
            aoAtlas.Usage = TextureUsage::LinearData;
            aoAtlas.Filter = TextureFilter::Linear;
            aoAtlas.Width = atlasLayout.Width;
            aoAtlas.Height = atlasLayout.Height;
            aoAtlas.Mips = { TextureMipLevel{ atlasLayout.Width,
                                              atlasLayout.Height, 0,
                                              aoPixels.size() } };
            aoAtlas.Blob.assign(aoPixels.begin(), aoPixels.end());

            const std::string aoRel = "levels/" + stemStr + "/ao.stex";
            const std::string aoAssetPath = "asset://" + aoRel;
            if (!textureSerializer.WriteToFile(
                    transaction.Stage(assetsRoot / ".cooked" / aoRel).generic_string(),
                    aoAtlas))
            {
                result.Error = "CookDocument: could not write AO atlas '" + aoRel + "'";
                return result;
            }
            artifacts.push_back(CookedArtifact{
                aoAssetPath, ".cooked/" + aoRel, AssetType::Texture });
            ambientOcclusionArtifact = artifacts.back();
            generatedMeshPaths.insert(aoAssetPath);
            materialRefs.push_back(aoAssetPath);
            lightmapFields.push_back({ "ao", JsonValue(aoAssetPath) });
        }
        cellEntities.push_back(JsonValue(JsonValue::Object{
            { "components", JsonValue(JsonValue::Object{
                { "ZoneLightmap", JsonValue(std::move(lightmapFields)) },
            }) },
        }));
        completeStep();
        if (bakeAo)
        {
            beginStep(CookStepIds::AmbientOcclusion);
            completeStep();
        }
        }
    }

    // Bake authored irradiance-probe volumes into the zone's .sprobe. Bounce
    // comes from Direct and Indirect lights against the same occlusion BVH;
    // the runtime locates the file by the cooked-scene path convention.
    if (!probeVolumes.empty())
    {
        beginStep(CookStepIds::IrradianceProbes);
        if (reusableProbes != nullptr)
        {
            std::string restoreError;
            if (!RestoreCookStepArtifacts(*reusableProbes, assetsRoot, transaction,
                                      artifacts, generatedMeshPaths, materialRefs,
                                      false, &restoreError))
            {
                result.Error = "CookDocument: " + restoreError;
                return result;
            }
            probeArtifact = artifacts.back();
            result.ReusedSteps.push_back(
                std::string(CookStepIds::IrradianceProbes));
            RestoreDocumentCookResultMetadata(reusableProbes->Metadata, result);
            completeStep();
        }
        else
        {
        ProbeBakeParams probeParams = lightmapParams.Probe;
        probeParams.Shading = lightmapParams.Shading;
        const std::vector<Vec3d> rayTable =
            BuildProbeRayTable(lightmapParams.ProbeRayCount);

        ProbeVolumeFile probeFile;
        std::size_t probeCount = 0;
        for (std::size_t i = 0; i < probeVolumes.size(); ++i)
        {
            if (cancelled())
                return result;
            const ProbeVolumeInput& input = probeVolumes[i];
            const ProbeVolumeBakeResult baked = BakeProbeVolume(
                input.Grid, occlusionBvh, bounceLights, rayTable, probeParams);

            ProbeVolumeRecord record;
            record.Grid = input.Grid;
            record.Priority = input.Priority;
            record.StableIndex = static_cast<std::uint32_t>(i);
            record.ShHalf = PackProbeShHalf(baked.Sh);
            record.ValidityBits = PackValidityBits(baked.Valid);
            probeCount += baked.Sh.size();
            probeFile.Volumes.push_back(std::move(record));
        }

        const std::string probeRel = "levels/" + stemStr + "/probes.sprobe";
        const std::filesystem::path probePhysical = assetsRoot / ".cooked" / probeRel;
        const std::filesystem::path probeStaged = transaction.Stage(probePhysical);
        std::error_code makeDirs;
        std::filesystem::create_directories(probeStaged.parent_path(), makeDirs);
        std::ofstream probeStream(probeStaged, std::ios::binary | std::ios::trunc);
        BinaryWriter probeWriter(probeStream);
        if (!probeStream.is_open() || !WriteProbeVolumeFile(probeWriter, probeFile))
        {
            result.Error = "CookDocument: could not write probe volumes '" + probeRel + "'";
            return result;
        }
        artifacts.push_back(CookedArtifact{
            "asset://" + probeRel, ".cooked/" + probeRel, AssetType::ProbeVolume });
        probeArtifact = artifacts.back();
        result.ProbeVolumeCount = probeVolumes.size();
        result.ProbeCount = probeCount;
        completeStep();
        }
    }

    for (const PendingCellMesh& pending : pendingMeshes)
    {
        if (!serializer.WriteToFile(pending.Physical.generic_string(), pending.Geometry))
        {
            result.Error = "CookDocument: could not write mesh '"
                + pending.Physical.generic_string() + "'";
            return result;
        }
    }

    JsonValue cooked = std::move(snapshot.PassthroughScene);
    beginStep(DocumentCookStepIds::CookedScene);
    bool appended = false;
    if (cooked.IsObject())
        for (auto& [key, value] : cooked.AsObject())
            if (key == "entities" && value.IsArray())
            {
                for (JsonValue& cellEntity : cellEntities)
                    value.AsArray().push_back(std::move(cellEntity));
                appended = true;
                break;
            }
    if (!appended)
    {
        result.Error = "CookDocument: assembled scene has no entities array";
        return result;
    }
    completeStep();
    if (cancelled())
        return result;

    std::error_code ec;
    const std::filesystem::path cookedDir = assetsRoot / ".cooked/levels";
    std::filesystem::create_directories(cookedDir, ec);
    const std::filesystem::path cookedScenePath = cookedDir / (stemStr + ".cooked.json");
    const std::filesystem::path manifestPath = cookedDir / (stemStr + ".manifest.json");
    const std::filesystem::path collisionSidecarPath =
        cookedDir / (stemStr + ".collision.json");

    // Collision sidecar: the runtime loads this at map load (LoadZoneCollision)
    // to spawn the level's static brush colliders. Empty array if no brushes.
    if (runCollision)
    {
        JsonValue::Array sidecar;
        sidecar.reserve(collisionEntries.size());
        for (const CollisionEntry& entry : collisionEntries)
            sidecar.push_back(JsonValue(JsonValue::Object{
                { "blob", JsonValue(entry.BlobRelPath) },
                { "origin", JsonValue(JsonValue::Array{
                    JsonValue(static_cast<double>(entry.Origin.X)),
                    JsonValue(static_cast<double>(entry.Origin.Y)),
                    JsonValue(static_cast<double>(entry.Origin.Z)) }) },
            }));
        std::ofstream sidecarFile(transaction.Stage(collisionSidecarPath));
        sidecarFile << JsonStringify(JsonValue(std::move(sidecar)), /*pretty*/ true);
        if (!sidecarFile.good())
        {
            result.Error = "CookDocument: could not write collision sidecar";
            return result;
        }
    }

    // asset:// resolution: Generated cell meshes live under .cooked/; every other
    // ref (materials, their textures) is an authored asset under the root.
    const auto physicalPathFor =
        [&assetsRoot, &generatedMeshPaths, &transaction](
            std::string_view assetPath) -> std::filesystem::path {
            constexpr std::string_view prefix = "asset://";
            const std::string rel(assetPath.substr(prefix.size()));
            if (generatedMeshPaths.count(std::string(assetPath)) != 0)
                return transaction.Stage(assetsRoot / ".cooked" / rel);
            return assetsRoot / rel;
        };

    const std::filesystem::path idMapPath = assetsRoot / kAssetIdMapFileName;
    std::string cookError;
    if (!transaction.Seed(idMapPath, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }
    const std::filesystem::path stagedManifest = transaction.Stage(manifestPath);
    const std::filesystem::path stagedScene = transaction.Stage(cookedScenePath);
    if (!WriteCookedScene(cooked, materialRefs, physicalPathFor,
            transaction.Stage(idMapPath), stagedManifest, stagedScene, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }

    // Prepare source textures the level's materials reference (.png -> .stex)
    // into private staging without touching active state. The bytes and their
    // cooked-index deltas publish through the document transaction below, so a
    // failed cook leaves no orphaned imports. Idempotent: unchanged sources are
    // served from the cooked cache.
    PendingAssetImport pendingImports;
    if (runReferencedAssets)
    {
        beginStep(CookStepIds::ReferencedAssets);
        PngTextureImporter textureImporter;
        AssetImporterRegistry importers;
        importers.Register(textureImporter);
        (void)PrepareAssetsOnDemand(assetsRoot, importers, logging, pendingImports);
        completeStep();
    }

    // Record source -> artifacts (source key = caller-supplied rel path, hash key
    // = brush-geometry hash). The index loads active (prepare left it untouched);
    // the prepared imports, their index deltas, and the level entry then publish
    // through this one staged index and the transaction, committing together.
    const std::filesystem::path indexPath = assetsRoot / ".cooked/index.json";
    CookedCacheIndex index;
    (void)CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index); // cold cache is fine
    if (runReferencedAssets)
    {
        std::unordered_set<std::string> documentArtifactPaths;
        for (const CookedArtifact& artifact : artifacts)
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
                    artifacts.push_back(artifact);
    }
    CookedSourceEntry entry;
    entry.SourceRelPath = std::string(sourceRel);
    entry.InputFingerprint = geometryHash;
    for (CookedArtifact& artifact : artifacts)
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
    entry.Artifacts = artifacts;
    index.Put(std::move(entry));
    if (!transaction.Seed(indexPath, &cookError)
        || !index.SaveToFile(transaction.Stage(indexPath).generic_string()))
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

    if (directLightmapArtifact.has_value() && !reuseLighting)
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
    if (ambientOcclusionArtifact.has_value() && !reuseLighting)
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
    if (probeArtifact.has_value() && reusableProbes == nullptr)
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
        .Artifacts = std::move(artifacts),
        .Metadata = DocumentCookResultMetadata(result),
    });
    if (!SaveDocumentCookReceipt(transaction.Stage(activeReceipt), receipt, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }
    beginStep(DocumentCookStepIds::Publication);
    if (cancelled())
        return result;
    if (!transaction.Commit(&cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }
    completeStep();

    result.Success = true;
    result.CookedScenePath = cookedScenePath;
    result.ManifestPath = manifestPath;
    result.CollisionSidecarPath = collisionSidecarPath;
    result.ContentHash = geometryHash;
    result.CellCount = cells.size();
    if (control != nullptr)
        control->CompletedSteps.store(control->TotalSteps.load());
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
