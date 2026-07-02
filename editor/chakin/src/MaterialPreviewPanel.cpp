#include "MaterialPreviewPanel.h"

#include "MaterialPreviewRenderFeature.h"

#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>

MaterialPreviewPanel::MaterialPreviewPanel(MaterialPreviewRenderFeature& preview)
    : Preview(preview)
{
}

void MaterialPreviewPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    int primitive = static_cast<int>(Preview.GetPrimitive());
    static constexpr const char* kPrimitives[] = { "Sphere", "Cube", "Plane" };
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##primitive", &primitive, kPrimitives, 3))
        Preview.SetPrimitive(static_cast<PreviewPrimitive>(primitive));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Light", &Preview.LightIntensity, 0.5f, 40.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const VkExtent2D extent{
        static_cast<uint32_t>(std::max(avail.x, 16.0f)),
        static_cast<uint32_t>(std::max(avail.y, 16.0f)),
    };

    const ImTextureID texture = Preview.Display(extent);
    if (texture == 0)
    {
        ImGui::TextDisabled("No preview yet.");
        return;
    }

    ImGui::Image(texture, avail);

    // Orbit while dragging over the image; wheel zooms when hovered.
    if (ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
            Preview.Zoom(wheel);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        Preview.Orbit(delta.x * 0.01f, delta.y * 0.01f);
    }
}
