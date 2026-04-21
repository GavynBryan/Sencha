#include "ViewportPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>

namespace
{
constexpr ImGuiWindowFlags kViewportChildFlags =
    ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoBackground;
}

ViewportPanel::ViewportPanel(ViewportLayout& layout)
    : Layout(layout)
{
}

std::string_view ViewportPanel::GetTitle() const
{
    return "Viewport";
}

bool ViewportPanel::IsVisible() const
{
    return Visible;
}

void ViewportPanel::OnDraw()
{
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(mainViewport->WorkSize, ImGuiCond_Always);

    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(GetTitle().data(), &Visible, windowFlags))
    {
        ImGui::End();
        return;
    }

    DrawNode(Layout.Tree(), ImGui::GetContentRegionAvail());

    ImGui::End();
}

void ViewportPanel::DrawNode(const LayoutNode& node, ImVec2 size)
{
    if (node.Kind == LayoutNode::NodeKind::Leaf)
    {
        EditorViewport* viewport = Layout.Find(node.Viewport);
        if (viewport == nullptr)
            return;

        ImGui::PushID(static_cast<int>(viewport->Id.Value));
        DrawViewport(*viewport, size);
        ImGui::PopID();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    if (node.SplitAxis == LayoutNode::Axis::Horizontal)
    {
        const float width = std::max(0.0f, size.x - style.ItemSpacing.x);
        const float firstWidth = width * node.Ratio;
        const float secondWidth = width - firstWidth;
        DrawNode(*node.First, ImVec2(firstWidth, size.y));
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        DrawNode(*node.Second, ImVec2(secondWidth, size.y));
        return;
    }

    const float height = std::max(0.0f, size.y - style.ItemSpacing.y);
    const float firstHeight = height * node.Ratio;
    const float secondHeight = height - firstHeight;
    DrawNode(*node.First, ImVec2(size.x, firstHeight));
    DrawNode(*node.Second, ImVec2(size.x, secondHeight));
}

void ViewportPanel::DrawViewport(EditorViewport& viewport, ImVec2 size)
{
    ImGui::BeginChild("ViewportLeaf", size, ImGuiChildFlags_Borders, kViewportChildFlags);

    DrawOrientationSelector(viewport);

    const ImVec2 renderSize(
        std::max(0.0f, ImGui::GetContentRegionAvail().x),
        std::max(0.0f, ImGui::GetContentRegionAvail().y));
    ImGui::BeginChild("ViewportRegion", renderSize, ImGuiChildFlags_None, kViewportChildFlags);

    viewport.RegionMin = ImGui::GetWindowPos();
    viewport.RegionMax = ImVec2(viewport.RegionMin.x + ImGui::GetWindowSize().x,
                                viewport.RegionMin.y + ImGui::GetWindowSize().y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 borderColor = viewport.IsActive
        ? IM_COL32(110, 170, 255, 255)
        : IM_COL32(70, 70, 70, 255);
    drawList->AddRect(viewport.RegionMin, viewport.RegionMax, borderColor);

    ImGui::EndChild();
    ImGui::EndChild();
}

void ViewportPanel::DrawOrientationSelector(EditorViewport& viewport)
{
    if (viewport.Orientation == ViewportOrientation::Perspective)
    {
        ImGui::TextUnformatted(viewport.GetDisplayLabel());
        return;
    }

    const char* preview = viewport.GetDisplayLabel();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##Orientation", preview))
        return;

    for (ViewportOrientation orientation : AllViewportOrientations())
    {
        const bool selected = viewport.Orientation == orientation;
        if (ImGui::Selectable(Traits(orientation).Label, selected))
            viewport.ApplyOrientation(orientation);
        if (selected)
            ImGui::SetItemDefaultFocus();
    }

    ImGui::EndCombo();
}
