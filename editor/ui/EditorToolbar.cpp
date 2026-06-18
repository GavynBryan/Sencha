#include "EditorToolbar.h"

#include "EditorUiStyle.h"
#include "fonts/IconsFontAwesome6.h"

#include "../meshedit/MeshElementKind.h"
#include "../meshedit/MeshEditService.h"
#include "../tools/ITool.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/GridSettings.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <cstdio>
#include <string>

namespace
{
// A square icon button that paints itself with the accent when active. The
// highlight color is a palette constant (ui_color_discipline-clean: no inline
// ImVec4 in PushStyleColor). Returns true when clicked.
bool ToolButton(const char* icon, const char* tooltip, bool active, float size)
{
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, EditorUi::AccentDim);
    const bool clicked = ImGui::Button(icon, ImVec2(size, size));
    if (active)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && tooltip != nullptr)
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}
}

EditorToolbar::EditorToolbar(ToolRegistry& tools, MeshEditService& meshEdit, GridSettings& grid)
    : Tools(tools)
    , MeshEdit(meshEdit)
    , Grid(grid)
{
}

void EditorToolbar::Draw()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonSize = ImGui::GetFrameHeight();
    const float barHeight = buttonSize + style.WindowPadding.y * 2.0f;

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginViewportSideBar("##EditorToolbar", viewport, ImGuiDir_Up, barHeight, flags))
    {
        // Tools (Select/Brush/Camera): drive the ToolRegistry.
        const auto& tools = Tools.GetTools();
        for (std::size_t i = 0; i < tools.size(); ++i)
        {
            const ITool* tool = tools[i].get();
            if (tool == nullptr)
                continue;

            if (i != 0)
                ImGui::SameLine();

            const std::string_view iconView = tool->GetIcon();
            const std::string label =
                (iconView.empty() ? std::string(tool->GetDisplayName()) : std::string(iconView))
                + "##tool" + std::to_string(i);
            const bool active = Tools.GetActiveIndex() == static_cast<int>(i);
            if (ToolButton(label.c_str(), tool->GetDisplayName().data(), active, buttonSize))
                Tools.Activate(i);
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");

        // Mesh element mode (Object/Vertex/Edge/Face): drives MeshEditService.
        struct ModeEntry { const char* Icon; const char* Name; MeshElementKind Kind; };
        static const ModeEntry kModes[] = {
            { ICON_FA_CUBE,          "Object", MeshElementKind::Object },
            { ICON_FA_CIRCLE_DOT,    "Vertex", MeshElementKind::Vertex },
            { ICON_FA_GRIP_LINES,    "Edge",   MeshElementKind::Edge   },
            { ICON_FA_VECTOR_SQUARE, "Face",   MeshElementKind::Face   },
        };
        for (const ModeEntry& mode : kModes)
        {
            ImGui::SameLine();
            const std::string label = std::string(mode.Icon) + "##mode" + mode.Name;
            const bool active = MeshEdit.GetElementKind() == mode.Kind;
            if (ToolButton(label.c_str(), mode.Name, active, buttonSize))
                MeshEdit.SetElementKind(mode.Kind);
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");

        // Grid snap toggle + spacing — drives the shared GridSettings, so picking,
        // manipulators and brush-create all honor it.
        ImGui::SameLine();
        if (ToolButton(ICON_FA_MAGNET "##snap",
                       Grid.SnapEnabled ? "Grid snap: on" : "Grid snap: off",
                       Grid.SnapEnabled, buttonSize))
            Grid.SnapEnabled = !Grid.SnapEnabled;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(96.0f);
        static const float kGridSizes[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
        char preview[32];
        std::snprintf(preview, sizeof(preview), ICON_FA_BORDER_ALL "  %g", Grid.Spacing);
        if (ImGui::BeginCombo("##gridsize", preview))
        {
            for (float size : kGridSizes)
            {
                char item[16];
                std::snprintf(item, sizeof(item), "%g", size);
                if (ImGui::Selectable(item, Grid.Spacing == size))
                    Grid.Spacing = size;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Grid size");
    }
    ImGui::End();
}
