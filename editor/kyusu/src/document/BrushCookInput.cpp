#include "BrushCookInput.h"

#include "SceneBrushWalk.h"
#include "brush/BrushLightmapCharts.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

#include <span>

std::vector<CookBrushGeometry> CollectCookBrushes(const EditorScene& scene,
                                                  const AssetRef& levelDefault,
                                                  CookChartSet* charts,
                                                  float chartConeDegrees,
                                                  float luxelSize)
{
    std::vector<CookBrushGeometry> brushes;

    // Renderers pass skipLocked=false; the cook follows suit — a locked brush is
    // still part of the level geometry, locking is an editing affordance only.
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId, const BrushMesh& mesh, const Transform3f& transform) {
            // Chart ids are per-brush from the generator; offset by the running
            // document-global count so every chart in the cook is distinct.
            BrushChartSet brushCharts;
            std::uint32_t chartBase = 0;
            if (charts != nullptr)
            {
                brushCharts = BuildBrushLightmapCharts(
                    mesh, transform, chartConeDegrees, luxelSize);
                chartBase = static_cast<std::uint32_t>(charts->Extents.size());
            }

            CookBrushGeometry brush;
            brush.WorldBounds = Aabb3d::Empty();

            BrushTessellate(mesh, transform,
                [&](std::uint32_t faceIndex, const FaceMaterial& material,
                    std::span<const BrushTriVertex> triangles) {
                    CookFace face;
                    face.Material = EffectiveMaterial(material, levelDefault);
                    face.Triangles.reserve(triangles.size());
                    const BrushLightmapChart* chart = nullptr;
                    if (charts != nullptr && faceIndex < brushCharts.FaceChart.size())
                    {
                        const std::uint32_t local = brushCharts.FaceChart[faceIndex];
                        chart = &brushCharts.Charts[local];
                        face.Chart = chartBase + local;
                        face.ChartUv.reserve(triangles.size());
                    }
                    for (const BrushTriVertex& v : triangles)
                    {
                        face.Triangles.push_back(StaticMeshVertex{
                            .Position = v.Position,
                            .Normal = v.Normal,
                            .Uv0 = v.Uv,
                            .Tangent = Vec4{},
                        });
                        if (chart != nullptr)
                            face.ChartUv.push_back(BrushChartUv(*chart, v.Position));
                        brush.WorldBounds.ExpandToInclude(v.Position);
                    }
                    brush.Faces.push_back(std::move(face));
                });

            if (charts != nullptr)
                for (const BrushLightmapChart& chart : brushCharts.Charts)
                    charts->Extents.push_back(Vec2d{ chart.UvMax.X - chart.UvMin.X,
                                                     chart.UvMax.Y - chart.UvMin.Y });

            // An all-faces-deleted brush yields no geometry; drop it so it never
            // becomes an empty cell assignment.
            if (!brush.Faces.empty())
                brushes.push_back(std::move(brush));
        });

    return brushes;
}
