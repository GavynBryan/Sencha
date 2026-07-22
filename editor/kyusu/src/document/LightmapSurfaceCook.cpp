#include "LightmapSurfaceCook.h"

#include "EditorDocument.h"

#include <core/logging/LoggingProvider.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace
{
    // Unorm16 lightmap UV component (texel-center convention).
    std::uint16_t PackLightmapUv16(float value)
    {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
    }

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

bool LayoutLightmapSurfaces(std::vector<BrushCell>& cells,
                            std::vector<LightmapPlacement>& placements,
                            const CookChartSet& charts,
                            const LightingCookParams& params,
                            JsonValue& passthroughScene,
                            LoggingProvider& logging,
                            LightmapAtlasLayout& outLayout,
                            std::string* error)
{
    // Placements pack into the same zone atlas as the brush charts: one rect per
    // placement, sized by the world span its [0,1] sheet covers.
    std::vector<Vec2d> extents = charts.Extents;
    for (LightmapPlacement& placement : placements)
    {
        placement.Chart = static_cast<std::uint32_t>(extents.size());
        extents.push_back(placement.WorldExtent);
    }
    outLayout = PackLightmapAtlas(extents, params.LuxelSize, params.MaxAtlasSize);
    const float luxel = outLayout.EffectiveLuxelSize;

    // Per-placement scale/bias: the mesh's sheet UVs map linearly into its rect's
    // grid points; the runtime applies uv * xy + zw. Written into the cook
    // document's component so SaveSceneJson serializes it.
    for (const LightmapPlacement& placement : placements)
    {
        const LightmapChartRect& rect = outLayout.Rects[placement.Chart];
        const float pointsU = std::ceil(placement.WorldExtent.X / luxel);
        const float pointsV = std::ceil(placement.WorldExtent.Y / luxel);
        const Vec4 scaleBias{
            pointsU / static_cast<float>(outLayout.Width),
            pointsV / static_cast<float>(outLayout.Height),
            (static_cast<float>(rect.X + kLightmapGutter) + 0.5f)
                / static_cast<float>(outLayout.Width),
            (static_cast<float>(rect.Y + kLightmapGutter) + 0.5f)
                / static_cast<float>(outLayout.Height) };
        if (!SetLightmapScaleBias(passthroughScene, placement.SceneEntityIndex, scaleBias))
        {
            if (error != nullptr)
                *error = "could not apply placement lightmap layout";
            return false;
        }
    }

    for (BrushCell& cell : cells)
        for (CookFace& face : cell.Faces)
        {
            if (face.Chart >= outLayout.Rects.size())
                continue;
            const LightmapChartRect& rect = outLayout.Rects[face.Chart];
            for (std::size_t i = 0; i < face.Triangles.size()
                 && i < face.ChartUv.size(); ++i)
            {
                // Grid point k maps to the CENTER of texel (rect + gutter + k);
                // bilinear filtering then interpolates exactly between adjacent
                // grid samples.
                const float u = (static_cast<float>(rect.X + kLightmapGutter)
                    + face.ChartUv[i].X / luxel + 0.5f)
                    / static_cast<float>(outLayout.Width);
                const float v = (static_cast<float>(rect.Y + kLightmapGutter)
                    + face.ChartUv[i].Y / luxel + 0.5f)
                    / static_cast<float>(outLayout.Height);
                face.Triangles[i].LightmapU = PackLightmapUv16(u);
                face.Triangles[i].LightmapV = PackLightmapUv16(v);
            }
        }

    if (luxel != params.LuxelSize)
        logging.GetLogger<EditorDocument>().Warn(
            "cook: lightmap atlas overflowed {}x{}; luxel size clamped {} -> {}",
            params.MaxAtlasSize, params.MaxAtlasSize, params.LuxelSize, luxel);
    return true;
}
