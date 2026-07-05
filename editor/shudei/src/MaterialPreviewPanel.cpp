#include "MaterialPreviewPanel.h"

#include "MaterialPreviewRenderFeature.h"

#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    // "asset://materials/dev/gray.smat" -> "gray"
    std::string_view TabLeafName(std::string_view virtualPath)
    {
        std::string_view name = virtualPath;
        if (const std::size_t slash = name.rfind('/'); slash != std::string_view::npos)
            name.remove_prefix(slash + 1);
        if (name.size() >= 5 && name.substr(name.size() - 5) == ".smat")
            name.remove_suffix(5);
        return name;
    }
}

MaterialPreviewPanel::MaterialPreviewPanel(MaterialPreviewRenderFeature& preview,
                                           MaterialTabSet& tabs,
                                           std::function<void(std::size_t)> closeTab)
    : Preview(preview)
    , Tabs(tabs)
    , CloseTab(std::move(closeTab))
{
}

void MaterialPreviewPanel::DrawTabBar()
{
    if (Tabs.Tabs().empty())
        return;
    if (!ImGui::BeginTabBar("##materials",
                            ImGuiTabBarFlags_FittingPolicyScroll
                                | ImGuiTabBarFlags_AutoSelectNewTabs))
        return;

    std::optional<std::size_t> closeRequest;
    for (std::size_t i = 0; i < Tabs.Tabs().size(); ++i)
    {
        const MaterialEditTab& tab = *Tabs.Tabs()[i];
        const std::string_view path = tab.Session.VirtualPath();

        // Leaf name for the label, full path for the ID (### keeps the tab
        // stable when the dirty marker changes) and duplicates distinct.
        std::string label(TabLeafName(path));
        label += "###";
        label += path;

        bool open = true;
        ImGuiTabItemFlags flags = tab.Session.IsDirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
        if (ImGui::BeginTabItem(label.c_str(), &open, flags))
        {
            if (Tabs.ActiveIndex() != i)
                Tabs.SetActive(i);
            ImGui::EndTabItem();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%.*s", static_cast<int>(path.size()), path.data());
        if (!open)
            closeRequest = i;
    }
    ImGui::EndTabBar();

    if (closeRequest && CloseTab)
        CloseTab(*closeRequest);
}

void MaterialPreviewPanel::DrawPreviewImage()
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 8.0f || avail.y < 8.0f)
        return;
    const VkExtent2D extent{
        static_cast<uint32_t>(avail.x),
        static_cast<uint32_t>(avail.y),
    };

    const ImTextureID texture = Preview.Display(extent);
    if (texture == 0)
    {
        ImGui::TextDisabled("No preview yet.");
        return;
    }

    // An Image is never an active item, so the input surface is an invisible
    // button laid over it: that is what makes drag-to-orbit register.
    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    ImGui::Image(texture, avail);
    ImGui::SetCursorScreenPos(imagePos);
    ImGui::InvisibleButton("##orbit", avail, ImGuiButtonFlags_MouseButtonLeft);

    if (ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
            Preview.Zoom(wheel);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        // Turntable feel: drag right orbits right, drag up orbits above.
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        Preview.Orbit(delta.x * 0.01f, -delta.y * 0.01f);
    }
}

void MaterialPreviewPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DrawTabBar();

    int primitive = static_cast<int>(Preview.GetPrimitive());
    static constexpr const char* kPrimitives[] = { "Sphere", "Cube", "Plane" };
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##primitive", &primitive, kPrimitives, 3))
        Preview.SetPrimitive(static_cast<PreviewPrimitive>(primitive));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Light", &Preview.LightIntensity, 0.5f, 40.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);

    DrawPreviewImage();
}
