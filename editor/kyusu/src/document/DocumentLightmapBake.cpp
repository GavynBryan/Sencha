#include "DocumentLightmapBake.h"

#include "CookArtifactTransaction.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookReuse.h"

#include <assets/cook/AmbientOcclusionBake.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapRaster.h>
#include <assets/cook/ProbeBake.h>
#include <assets/cook/TextureCook.h>
#include <assets/texture/TextureSerializer.h>
#include <project/CookProfile.h>
#include <render/static_mesh/MeshGeometry.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Restore a reusable prior lightmap bake: the direct atlas and, when enabled,
    // the AO plane, both re-staged through the transaction. Appends the
    // ZoneLightmap entity referencing the restored artifacts.
    bool RestoreLightmap(const DocumentCookSnapshot& snapshot,
                         const DocumentCookReuse& reuse,
                         const std::filesystem::path& assetsRoot,
                         CookArtifactTransaction& transaction,
                         DocumentArtifactCatalog& catalog, CookStepProgress& progress,
                         JsonValue::Array& cellEntities,
                         std::optional<CookedArtifact>& directArtifact,
                         std::optional<CookedArtifact>& aoArtifact,
                         DocumentCookResult& result)
    {
        std::string restoreError;
        progress.Begin(CookStepIds::DirectLightmap);
        CookedArtifact restoredDirect;
        if (!catalog.RestoreStep(*reuse.Direct, assetsRoot, transaction, true,
                                 restoredDirect, &restoreError))
        {
            result.Error = "CookDocument: " + restoreError;
            return false;
        }
        directArtifact = restoredDirect;
        result.ReusedSteps.push_back(std::string(CookStepIds::DirectLightmap));
        RestoreDocumentCookResultMetadata(reuse.Direct->Metadata, result);
        progress.Complete();

        JsonValue::Object lightmapFields{
            { "texture", JsonValue(directArtifact->Path) },
        };
        if (snapshot.Lighting.Ao.Enabled)
        {
            progress.Begin(CookStepIds::AmbientOcclusion);
            CookedArtifact restoredAo;
            if (!catalog.RestoreStep(*reuse.Ao, assetsRoot, transaction, true,
                                     restoredAo, &restoreError))
            {
                result.Error = "CookDocument: " + restoreError;
                return false;
            }
            aoArtifact = restoredAo;
            result.ReusedSteps.push_back(std::string(CookStepIds::AmbientOcclusion));
            lightmapFields.push_back({ "ao", JsonValue(aoArtifact->Path) });
            progress.Complete();
        }
        cellEntities.push_back(JsonValue(JsonValue::Object{
            { "components", JsonValue(JsonValue::Object{
                { "ZoneLightmap", JsonValue(std::move(lightmapFields)) },
            }) },
        }));
        return true;
    }

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

    // Bake the direct atlas and, when enabled, the AO plane fresh, write both as
    // cooked textures, and append the ZoneLightmap entity referencing them.
    bool BakeFreshLightmap(const DocumentCookSnapshot& snapshot,
                           const std::vector<BrushCell>& cells,
                           const LightmapAtlasLayout& atlasLayout,
                           const BakeBvh& occlusionBvh,
                           const std::filesystem::path& assetsRoot, std::string_view stem,
                           CookArtifactTransaction& transaction,
                           DocumentArtifactCatalog& catalog, CookStepProgress& progress,
                           LoggingProvider& logging, JsonValue::Array& cellEntities,
                           std::optional<CookedArtifact>& directArtifact,
                           std::optional<CookedArtifact>& aoArtifact,
                           DocumentCookResult& result)
    {
        const LightingCookParams& params = snapshot.Lighting;
        const std::string stemStr(stem);
        progress.Begin(CookStepIds::DirectLightmap);

        std::vector<std::vector<LightmapRasterTriangle>> chartTriangles =
            GatherChartTriangles(snapshot, cells, atlasLayout);

        std::vector<std::uint32_t> atlasPixels(
            static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height, 0u);
        // The AO plane initializes white: texels no chart covers (including the
        // reserved border and the (0, 0) texel unbaked items sample) must never
        // darken the ambient term.
        const bool bakeAo = params.Ao.Enabled;
        std::vector<std::uint8_t> aoPixels;
        std::vector<Vec3d> aoRayTable;
        AmbientOcclusionBakeParams aoBake;
        if (bakeAo)
        {
            aoPixels.assign(
                static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height, 255u);
            aoRayTable = BuildProbeRayTable(params.Ao.RayCount);
            aoBake.MaxDistance = params.Ao.MaxDistance;
            aoBake.NormalOffset = params.Shading.NormalOffset;
            aoBake.RayTable = aoRayTable;
        }
        for (std::size_t c = 0; c < chartTriangles.size(); ++c)
        {
            if (progress.Cancelled(result))
                return false;
            BakeChartLuxels(chartTriangles[c], atlasLayout.Rects[c], snapshot.BakeLights,
                            occlusionBvh, params.Shading, atlasLayout.Width, atlasPixels,
                            bakeAo ? &aoBake : nullptr, aoPixels);
        }

        // Emit the atlas as a cooked texture artifact plus the zone component that
        // binds it at runtime.
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
            return false;
        }
        directArtifact = catalog.AddSceneTexture(atlasAssetPath, ".cooked/" + atlasRel);

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
            aoAtlas.Mips = { TextureMipLevel{ atlasLayout.Width, atlasLayout.Height, 0,
                                              aoPixels.size() } };
            aoAtlas.Blob.assign(aoPixels.begin(), aoPixels.end());

            const std::string aoRel = "levels/" + stemStr + "/ao.stex";
            const std::string aoAssetPath = "asset://" + aoRel;
            if (!textureSerializer.WriteToFile(
                    transaction.Stage(assetsRoot / ".cooked" / aoRel).generic_string(),
                    aoAtlas))
            {
                result.Error = "CookDocument: could not write AO atlas '" + aoRel + "'";
                return false;
            }
            aoArtifact = catalog.AddSceneTexture(aoAssetPath, ".cooked/" + aoRel);
            lightmapFields.push_back({ "ao", JsonValue(aoAssetPath) });
        }
        cellEntities.push_back(JsonValue(JsonValue::Object{
            { "components", JsonValue(JsonValue::Object{
                { "ZoneLightmap", JsonValue(std::move(lightmapFields)) },
            }) },
        }));
        progress.Complete();
        if (bakeAo)
        {
            progress.Begin(CookStepIds::AmbientOcclusion);
            progress.Complete();
        }
        return true;
    }
} // namespace

bool BakeDocumentLightmap(const DocumentCookSnapshot& snapshot,
                          const std::vector<BrushCell>& cells,
                          const LightmapAtlasLayout& atlasLayout,
                          const BakeBvh& occlusionBvh, const DocumentCookReuse& reuse,
                          const std::filesystem::path& assetsRoot, std::string_view stem,
                          CookArtifactTransaction& transaction,
                          DocumentArtifactCatalog& catalog, CookStepProgress& progress,
                          LoggingProvider& logging, JsonValue::Array& cellEntities,
                          std::optional<CookedArtifact>& directArtifact,
                          std::optional<CookedArtifact>& aoArtifact,
                          DocumentCookResult& result)
{
    result.DirectLightCount = snapshot.BakeLights.size();
    result.LightmapAtlasWidth = atlasLayout.Width;
    result.LightmapAtlasHeight = atlasLayout.Height;
    result.EffectiveLuxelSize = atlasLayout.EffectiveLuxelSize;
    if (reuse.ReuseLighting)
        return RestoreLightmap(snapshot, reuse, assetsRoot, transaction, catalog,
                               progress, cellEntities, directArtifact, aoArtifact,
                               result);
    return BakeFreshLightmap(snapshot, cells, atlasLayout, occlusionBvh, assetsRoot,
                             stem, transaction, catalog, progress, logging, cellEntities,
                             directArtifact, aoArtifact, result);
}
