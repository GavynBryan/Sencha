#include "DocumentLightmapBake.h"

#include "CookArtifactPaths.h"
#include "CookArtifactTransaction.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookContext.h"
#include "DocumentCookReuse.h"

#include <assets/cook/AmbientOcclusionBake.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapRaster.h>
#include <assets/cook/ProbeBake.h>
#include <assets/cook/TextureCook.h>
#include <assets/texture/TextureSerializer.h>
#include <project/CookProfile.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Gather each chart's world triangles (positions + smoothed normals + chart
    // grid UVs) from the pre-weld cell faces, then the placement charts. Each
    // chart's span holds only its own surface: a boundary luxel bakes the
    // one-sided limit of its own chart, so an illumination step on a chart seam
    // stays crisp instead of averaging both sides into the shared boundary texels.
    std::vector<std::vector<LightmapRasterTriangle>> GatherChartTriangles(
        const DocumentCookSnapshot& snapshot, const std::vector<BrushCell>& cells,
        const LightmapAtlasLayout& atlasLayout)
    {
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

        // Placement charts: sheet UVs scale into the rect's grid points; the luxel
        // bake then runs on the placed world triangles like any chart.
        for (const LightmapPlacement& placement : snapshot.Placements)
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
        return chartTriangles;
    }

    // Resolve one sample map per chart. Direct and AO both bake over these, so
    // the expensive rasterization runs once even when both channels are fresh.
    std::vector<LightmapSurfaceSamples> ResolveChartSampleMaps(
        const DocumentCookSnapshot& snapshot, const std::vector<BrushCell>& cells,
        const LightmapAtlasLayout& atlasLayout, const BakeBvh& occluders)
    {
        const std::vector<std::vector<LightmapRasterTriangle>> chartTriangles =
            GatherChartTriangles(snapshot, cells, atlasLayout);
        std::vector<LightmapSurfaceSamples> maps;
        maps.reserve(chartTriangles.size());
        for (std::size_t c = 0; c < chartTriangles.size(); ++c)
            maps.push_back(ResolveLightmapSurfaceSamples(
                chartTriangles[c], atlasLayout.Rects[c], occluders,
                snapshot.Lighting.Shading.NormalOffset));
        return maps;
    }

    // Restore a reusable channel's cached .stex through the transaction.
    bool RestoreChannel(const CookStepReceipt& reusable,
                        const std::filesystem::path& assetsRoot,
                        CookArtifactTransaction& transaction,
                        DocumentArtifactCatalog& catalog,
                        std::optional<CookedArtifact>& artifact, std::string* error)
    {
        CookedArtifact restored;
        if (!catalog.RestoreStep(reusable, assetsRoot, transaction, true, restored, error))
            return false;
        artifact = restored;
        return true;
    }

    bool BakeFreshDirect(const DocumentCookContext& ctx,
                         const std::vector<LightmapSurfaceSamples>& sampleMaps,
                         const LightmapAtlasLayout& atlasLayout,
                         const DocumentCookSnapshot& snapshot, const BakeBvh& occluders,
                         std::optional<CookedArtifact>& directArtifact)
    {
        DocumentCookResult& result = ctx.Result;
        CookStepProgress& progress = ctx.Progress;
        std::vector<std::uint32_t> atlasPixels(
            static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height, 0u);
        for (std::size_t c = 0; c < sampleMaps.size(); ++c)
        {
            if (progress.Cancelled(result))
                return false;
            BakeDirectLightmapChart(sampleMaps[c], atlasLayout.Rects[c], snapshot.BakeLights,
                                    occluders, snapshot.Lighting.Shading, atlasLayout.Width,
                                    atlasPixels);
        }

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

        const std::string atlasRel = LightmapAtlasRel(ctx.Stem());
        const std::string atlasAssetPath = "asset://" + atlasRel;
        TextureSerializer textureSerializer(ctx.Logging);
        if (!textureSerializer.WriteToFile(
                ctx.Transaction.Stage(ctx.AssetsRoot / ".cooked" / atlasRel).generic_string(),
                atlas))
        {
            result.Error = "CookDocument: could not write lightmap atlas '" + atlasRel + "'";
            return false;
        }
        directArtifact = ctx.Catalog.AddSceneTexture(atlasAssetPath, ".cooked/" + atlasRel);
        return true;
    }

    bool BakeFreshAo(const DocumentCookContext& ctx,
                     const std::vector<LightmapSurfaceSamples>& sampleMaps,
                     const LightmapAtlasLayout& atlasLayout,
                     const DocumentCookSnapshot& snapshot, const BakeBvh& occluders,
                     std::optional<CookedArtifact>& aoArtifact)
    {
        DocumentCookResult& result = ctx.Result;
        CookStepProgress& progress = ctx.Progress;
        const LightingCookParams& params = snapshot.Lighting;
        // The AO plane initializes white: texels no chart covers (including the
        // reserved border and the (0, 0) texel unbaked items sample) must never
        // darken the ambient term.
        std::vector<std::uint8_t> aoPixels(
            static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height, 255u);
        const std::vector<Vec3d> rayTable = BuildProbeRayTable(params.Ao.RayCount);
        AmbientOcclusionBakeParams aoBake;
        aoBake.MaxDistance = params.Ao.MaxDistance;
        aoBake.NormalOffset = params.Shading.NormalOffset;
        aoBake.RayTable = rayTable;
        for (std::size_t c = 0; c < sampleMaps.size(); ++c)
        {
            if (progress.Cancelled(result))
                return false;
            BakeAmbientOcclusionChart(sampleMaps[c], atlasLayout.Rects[c], occluders, aoBake,
                                      atlasLayout.Width, aoPixels);
        }

        TextureData aoAtlas;
        aoAtlas.Format = TexturePixelFormat::R8;
        aoAtlas.Usage = TextureUsage::LinearData;
        aoAtlas.Filter = TextureFilter::Linear;
        aoAtlas.Width = atlasLayout.Width;
        aoAtlas.Height = atlasLayout.Height;
        aoAtlas.Mips = { TextureMipLevel{ atlasLayout.Width, atlasLayout.Height, 0,
                                          aoPixels.size() } };
        aoAtlas.Blob.assign(aoPixels.begin(), aoPixels.end());

        const std::string aoRel = AoAtlasRel(ctx.Stem());
        const std::string aoAssetPath = "asset://" + aoRel;
        TextureSerializer textureSerializer(ctx.Logging);
        if (!textureSerializer.WriteToFile(
                ctx.Transaction.Stage(ctx.AssetsRoot / ".cooked" / aoRel).generic_string(),
                aoAtlas))
        {
            result.Error = "CookDocument: could not write AO atlas '" + aoRel + "'";
            return false;
        }
        aoArtifact = ctx.Catalog.AddSceneTexture(aoAssetPath, ".cooked/" + aoRel);
        return true;
    }
} // namespace

bool BakeDocumentLightmap(const DocumentCookContext& ctx,
                          const DocumentCookSnapshot& snapshot,
                          const std::vector<BrushCell>& cells,
                          const LightmapAtlasLayout& atlasLayout,
                          const BakeBvh& occlusionBvh, const DocumentCookReuse& reuse,
                          JsonValue::Array& cellEntities,
                          std::optional<CookedArtifact>& directArtifact,
                          std::optional<CookedArtifact>& aoArtifact)
{
    const std::filesystem::path& assetsRoot = ctx.AssetsRoot;
    CookArtifactTransaction& transaction = ctx.Transaction;
    DocumentArtifactCatalog& catalog = ctx.Catalog;
    CookStepProgress& progress = ctx.Progress;
    DocumentCookResult& result = ctx.Result;

    result.DirectLightCount = snapshot.BakeLights.size();
    result.LightmapAtlasWidth = atlasLayout.Width;
    result.LightmapAtlasHeight = atlasLayout.Height;
    result.EffectiveLuxelSize = atlasLayout.EffectiveLuxelSize;

    const bool aoEnabled = snapshot.Lighting.Ao.Enabled;
    const bool directReuse = reuse.Direct != nullptr;
    const bool aoReuse = aoEnabled && reuse.Ao != nullptr;

    // Direct and AO share one sample map, so resolve it once when either channel
    // bakes fresh; both reused means no rasterization at all.
    std::vector<LightmapSurfaceSamples> sampleMaps;
    if (!directReuse || (aoEnabled && !aoReuse))
        sampleMaps = ResolveChartSampleMaps(snapshot, cells, atlasLayout, occlusionBvh);

    std::string error;
    progress.Begin(CookStepIds::DirectLightmap);
    if (directReuse)
    {
        if (!RestoreChannel(*reuse.Direct, assetsRoot, transaction, catalog, directArtifact,
                            &error))
        {
            result.Error = "CookDocument: " + error;
            return false;
        }
        result.ReusedSteps.push_back(std::string(CookStepIds::DirectLightmap));
        RestoreDocumentCookResultMetadata(reuse.Direct->Metadata, result);
    }
    else if (!BakeFreshDirect(ctx, sampleMaps, atlasLayout, snapshot, occlusionBvh,
                              directArtifact))
        return false;
    progress.Complete();

    if (aoEnabled)
    {
        progress.Begin(CookStepIds::AmbientOcclusion);
        if (aoReuse)
        {
            if (!RestoreChannel(*reuse.Ao, assetsRoot, transaction, catalog, aoArtifact, &error))
            {
                result.Error = "CookDocument: " + error;
                return false;
            }
            result.ReusedSteps.push_back(std::string(CookStepIds::AmbientOcclusion));
        }
        else if (!BakeFreshAo(ctx, sampleMaps, atlasLayout, snapshot, occlusionBvh, aoArtifact))
            return false;
        progress.Complete();
    }

    JsonValue::Object lightmapFields{
        { "texture", JsonValue(directArtifact->Path) },
    };
    if (aoEnabled)
        lightmapFields.push_back({ "ao", JsonValue(aoArtifact->Path) });
    cellEntities.push_back(JsonValue(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "ZoneLightmap", JsonValue(std::move(lightmapFields)) },
        }) },
    }));
    return true;
}
