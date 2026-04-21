#include "ViewportPanel.h"

#include <imgui.h>

#include <array>

ViewportPanel::ViewportPanel(FourWayViewportLayout& layout)
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

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 cellSize(
        (available.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f,
        (available.y - ImGui::GetStyle().ItemSpacing.y) * 0.5f);

    const std::array labels = { "Perspective", "Top", "Front", "Side" };

    for (int index = 0; index < 4; ++index)
    {
        if ((index % 2) != 0)
            ImGui::SameLine();

        ImGui::PushID(index);
        DrawViewport(Layout.Viewports[index], "ViewportRegion", labels[index], cellSize);
        ImGui::PopID();
    }

    ImGui::End();
}

void ViewportPanel::DrawViewport(EditorViewport& viewport,
                                 const char* id,
                                 const char* label,
                                 ImVec2 size)
{
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
    viewport.RegionMin = ImGui::GetWindowPos();
    viewport.RegionMax = ImVec2(viewport.RegionMin.x + ImGui::GetWindowSize().x,
                                viewport.RegionMin.y + ImGui::GetWindowSize().y);
    viewport.IsActive = Layout.ActiveIndex == (&viewport - Layout.Viewports);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(viewport.RegionMin, viewport.RegionMax, IM_COL32(70, 70, 70, 255));

    ImGui::SetCursorScreenPos(ImVec2(viewport.RegionMin.x + 8.0f, viewport.RegionMin.y + 6.0f));
    ImGui::TextUnformatted(label);
    ImGui::EndChild();
}
