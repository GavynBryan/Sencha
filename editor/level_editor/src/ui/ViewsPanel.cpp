#include "ViewsPanel.h"

#include "SceneBrowserPanel.h"
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

    for (const ChromeRow& row : Rows)
        DrawChromeRow(row);

    const ImVec2 size(std::max(0.0f, ImGui::GetContentRegionAvail().x),
                      std::max(0.0f, ImGui::GetContentRegionAvail().y));
    ImGui::BeginChild("ViewsRegion", size, ImGuiChildFlags_None, kViewsChildFlags);
    RegionHovered = ImGui::IsWindowHovered();
    Content = Rect2d(ToVec2d(ImGui::GetWindowPos()), ToVec2d(ImGui::GetWindowSize()));
    View.ContentHole = Content;

    ImGui::EndChild();

    // The region child is the last item: a scene dropped anywhere on it lands
    // in the view under the pointer.
    if (SceneDrop && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SceneBrowserPanel::kDragPayloadType))
        {
            const Vec2d at = ToVec2d(ImGui::GetMousePos());
            if (const ViewportId under = Layout.ResolveAt(at); under.IsValid())
                SceneDrop(under, at,
                          std::string_view(static_cast<const char*>(payload->Data),
                                           static_cast<std::size_t>(payload->DataSize) - 1));
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

void ViewsPanel::DrawChromeRow(const ChromeRow& row)
{
    if (!row.Height || !row.Draw)
        return;
    const float rowHeight = std::max(0.0f, row.Height());
    if (rowHeight <= 0.0f)
        return;
    // Reserved the way the title row is: the rect handed over, the cursor
    // moved under it, so what follows starts below the row.
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const ImVec2 rowMax(rowMin.x + std::max(0.0f, ImGui::GetContentRegionAvail().x), rowMin.y + rowHeight);
    row.Draw(ImGui::GetWindowDrawList(), rowMin, rowMax);
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y + EditorUi::Px(2.0f)));
}
