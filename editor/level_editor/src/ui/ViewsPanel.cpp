#include "ViewsPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ImGuiVec.h"
#include "ui/WorkspaceView.h"

#include <imgui.h>

#include <algorithm>

namespace
{
constexpr ImGuiWindowFlags kViewsChildFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoBackground;
}

ViewsPanel::ViewsPanel(ViewportLayout& layout, WorkspaceView& view)
    : Layout(layout)
    , View(view)
{
}

void ViewsPanel::OnDraw()
{
    RegionHovered = false;
    Content = {};

    // No background anywhere: the views under the panels show through.
    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin(GetWindowName().data(), &Visible, windowFlags);
    ImGui::PopStyleVar();
    if (!open)
    {
        ImGui::End();
        return;
    }

    const ImVec2 size(std::max(0.0f, ImGui::GetContentRegionAvail().x),
                      std::max(0.0f, ImGui::GetContentRegionAvail().y));
    ImGui::BeginChild("ViewsRegion", size, ImGuiChildFlags_None, kViewsChildFlags);
    RegionHovered = ImGui::IsWindowHovered();
    Content = Rect2d(ToVec2d(ImGui::GetWindowPos()), ToVec2d(ImGui::GetWindowSize()));
    View.ContentHole = Content;

    ImGui::EndChild();

    ImGui::End();
}
