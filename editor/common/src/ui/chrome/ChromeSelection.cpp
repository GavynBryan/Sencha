#include "ChromeSelection.h"
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
