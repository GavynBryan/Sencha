#include "BrushBake.h"

#include "brush/BrushLightmapCharts.h"
#include "brush/BrushMesh.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/LightmapAtlasPack.h>

#include <cmath>
#include <span>
#include <utility>

bool BakeBrushToGeometry(const BrushMesh& mesh,
                         const AssetRef& levelDefault,
                         MeshGeometry& out,
                         std::vector<AssetRef>& outMaterialOrder,
                         std::string* error)
{
    // Chart the brush and pack a private [0,1] lightmap sheet: a baked mesh
    // is instanceable, so its UVs are mesh-local and the level cook remaps
    // each placement into the zone atlas with a per-instance scale/bias. The
    // luxel size here only sets the sheet's relative proportions; the level
    // cook derives real world density from the placed triangles.
    constexpr float kSheetLuxel = 0.25f;
    const BrushChartSet charts =
        BuildBrushLightmapCharts(mesh, Transform3f::Identity(), 45.0f, kSheetLuxel);
    std::vector<Vec2d> extents;
    extents.reserve(charts.Charts.size());
    for (const BrushLightmapChart& chart : charts.Charts)
        extents.push_back(Vec2d{ chart.UvMax.X - chart.UvMin.X,
                                 chart.UvMax.Y - chart.UvMin.Y });
    const LightmapAtlasLayout sheet = PackLightmapAtlas(extents, kSheetLuxel, 2048);

    std::vector<CookFace> faces;
    BrushTessellate(mesh, Transform3f::Identity(),
        [&](std::uint32_t faceIndex, const FaceMaterial& material,
            std::span<const BrushTriVertex> triangles) {
            CookFace face;
            face.Material = EffectiveMaterial(material, levelDefault);
            face.Triangles.reserve(triangles.size());
            const BrushLightmapChart* chart = faceIndex < charts.FaceChart.size()
                ? &charts.Charts[charts.FaceChart[faceIndex]]
                : nullptr;
            const LightmapChartRect* rect = faceIndex < charts.FaceChart.size()
                ? &sheet.Rects[charts.FaceChart[faceIndex]]
                : nullptr;
            for (const BrushTriVertex& v : triangles)
            {
                StaticMeshVertex vertex{
                    .Position = v.Position,
                    .Normal = v.Normal,
                    .Uv0 = v.Uv,
                    .Tangent = Vec4{},
                };
                if (chart != nullptr && rect != nullptr)
                {
                    const Vec2d uv = BrushChartUv(*chart, v.Position);
                    const float su = (static_cast<float>(rect->X + kLightmapGutter)
                        + uv.X / sheet.EffectiveLuxelSize + 0.5f)
                        / static_cast<float>(sheet.Width);
                    const float sv = (static_cast<float>(rect->Y + kLightmapGutter)
                        + uv.Y / sheet.EffectiveLuxelSize + 0.5f)
                        / static_cast<float>(sheet.Height);
                    const auto pack = [](float value) {
                        const float clamped =
                            value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                        return static_cast<std::uint16_t>(clamped * 65535.0f + 0.5f);
                    };
                    vertex.LightmapU = pack(su);
                    vertex.LightmapV = pack(sv);
                }
                face.Triangles.push_back(vertex);
            }
            faces.push_back(std::move(face));
        });

    if (faces.empty())
    {
        if (error != nullptr)
            *error = "brush has no faces to bake";
        return false;
    }

    outMaterialOrder = CollectMaterialOrder(faces);
    return BakeBrushFacesToStaticMesh(faces, outMaterialOrder, out, error);
}
