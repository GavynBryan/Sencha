#pragma once

#include <imgui.h>

// Lays buttons left to right, wrapping to the next line when the next one would
// overflow the content region. Panels of verbs read as a wrapped cluster rather
// than a single column, at whatever width the panel happens to be docked to.
//
// Construct one per cluster: it captures the right edge at construction and
// tracks whether it is placing the first button of a row.
class ButtonFlow
{
public:
    ButtonFlow()
        : RightEdge(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x)
    {
    }

    bool Button(const char* label)
    {
        Place(label);
        return ImGui::Button(label);
    }

private:
    void Place(const char* label)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float width = ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
        if (!FirstInRow && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= RightEdge)
            ImGui::SameLine();
        FirstInRow = false;
    }

    float RightEdge = 0.0f;
    bool FirstInRow = true;
};
