#include <render/RenderDebugView.h>

namespace
{
    struct RenderDebugViewName
    {
        RenderDebugView View;
        const char* Name;
        const char* Label;
    };

    constexpr RenderDebugViewName kNames[] = {
        { RenderDebugView::None, "none", "None" },
        { RenderDebugView::WorldNormals, "world_normals", "World normals" },
        { RenderDebugView::NormalMap, "normal_map", "Normal-map sample" },
        { RenderDebugView::NormalDelta, "normal_delta", "Normal delta" },
        { RenderDebugView::Diffuse, "diffuse", "Diffuse only" },
        { RenderDebugView::Specular, "specular", "Specular only" },
        { RenderDebugView::Emission, "emission", "Emission only" },
        { RenderDebugView::Roughness, "roughness", "Roughness / exponent" },
        { RenderDebugView::LightComplexity, "light_complexity", "Light complexity" },
        { RenderDebugView::ShadowFiltered, "shadow", "Filtered shadow term" },
        { RenderDebugView::ShadowRaw, "shadow_raw", "Raw shadow compare" },
        { RenderDebugView::Overdraw, "overdraw", "Overdraw" },
        { RenderDebugView::BakedDirect, "baked_direct", "Baked direct" },
        { RenderDebugView::LightmapTexels, "lightmap_texels", "Lightmap texels" },
    };

    static_assert(sizeof(kNames) / sizeof(kNames[0]) == kRenderDebugViewCount);
}

const char* ToString(RenderDebugView view)
{
    for (const RenderDebugViewName& entry : kNames)
    {
        if (entry.View == view)
            return entry.Name;
    }
    return "none";
}

const char* RenderDebugViewLabel(RenderDebugView view)
{
    for (const RenderDebugViewName& entry : kNames)
    {
        if (entry.View == view)
            return entry.Label;
    }
    return "None";
}

bool ParseRenderDebugView(std::string_view name, RenderDebugView& view)
{
    for (const RenderDebugViewName& entry : kNames)
    {
        if (entry.Name == name)
        {
            view = entry.View;
            return true;
        }
    }
    return false;
}
