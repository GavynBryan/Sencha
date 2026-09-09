#include "ChromeSelection.h"

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
