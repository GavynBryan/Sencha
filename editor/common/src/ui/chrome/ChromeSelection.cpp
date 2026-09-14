#include "ChromeSelection.h"

#include "ChromeFrame.h"

#include <algorithm>
#include "ChromePaint.h"

#include "ui/EditorUiStyle.h"

#include <imgui.h>

ScopedSelectionStyle::ScopedSelectionStyle(bool selected)
    : Pushed(selected)
{
    if (!Pushed)
        return;
    ImGui::PushStyleColor(ImGuiCol_Header, EditorUi::Selected);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorUi::Lighten(EditorUi::Selected, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorUi::Selected);
}

ScopedSelectionStyle::~ScopedSelectionStyle()
{
    if (Pushed)
        ImGui::PopStyleColor(3);
}

void EditorChrome::SelectionOutline(ImDrawList* dl, ImVec2 mn, ImVec2 mx)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    const ImU32 color = ImGui::GetColorU32(EditorUi::SelectedOutline);
    const float edge = EditorUi::Px(EditorUi::Metrics.EdgeWidth);
    dl->AddRect(mn, mx, color, 0.0f, 0, edge * 2.0f);
    DrawBracketCorners(dl, mn, mx, edge * 6.0f, color, edge * 3.0f);
}

void EditorChrome::ContentBoundary(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, bool active)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    if (active)
    {
        SelectionOutline(dl, mn, mx);
        return;
    }
    const float edge = std::max(1.0f, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    if (ChromeSpecFor(style).Header != HeaderPlate::Bezel)
    {
        dl->AddRect(mn, mx, ImGui::GetColorU32(EditorUi::Border));
        return;
    }
    // A bezel's content keeps a trim of its own: the dim form of the amber that
    // marks the view being edited, over a dark inset so it reads as recessed.
    dl->AddRect(ImVec2(mn.x + edge, mn.y + edge), ImVec2(mx.x - edge, mx.y - edge),
                ImGui::GetColorU32(EditorUi::MetalShadow), 0.0f, 0, edge);
    dl->AddRect(mn, mx, ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::SelectedOutline, 0.45f)), 0.0f, 0, edge);
}

void EditorChrome::SelectionMark()
{
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const float left = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Selected, 0.12f)));
    dl->AddRectFilled(ImVec2(left, mn.y), ImVec2(left + EditorUi::Px(2.0f), mx.y),
                      ImGui::GetColorU32(EditorUi::SelectedOutline));
}
