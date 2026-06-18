#include "EditorToolbar.h"

#include "EditorUiSkin.h"
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
// A square glossy beveled icon button (EditorUiSkin), accent-lit when active.
// `id` is a stable ImGui id; `tooltip` may be dynamic. Returns true when clicked.
bool ToolButton(const char* id, const char* icon, const char* tooltip, bool active, float size)
{
    const bool clicked = EditorUiSkin::Button(id, icon, ImVec2(size, size), active);
    if (tooltip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

// Vertical divider matching the button height, for grouping the toolbar sections.
void Divider(float height)
{
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x + 3.0f, p.y), ImVec2(p.x + 3.0f, p.y + height),
        ImGui::GetColorU32(EditorUiSkin::WithAlpha(EditorUi::Border, 0.9f)));
    ImGui::Dummy(ImVec2(7.0f, height));
    ImGui::SameLine();
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
        // Glossy metal band behind the buttons.
        EditorUiSkin::Band(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                           ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                           EditorUi::HeaderBg);

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
            const std::string icon(iconView.empty() ? tool->GetDisplayName() : iconView);
            const bool active = Tools.GetActiveIndex() == static_cast<int>(i);
            if (ToolButton(tool->GetId().data(), icon.c_str(), tool->GetDisplayName().data(), active, buttonSize))
                Tools.Activate(i);
        }

        Divider(buttonSize);

        // Mesh element mode (Object/Vertex/Edge/Face): drives MeshEditService.
        struct ModeEntry { const char* Icon; const char* Name; MeshElementKind Kind; };
        static const ModeEntry kModes[] = {
            { ICON_FA_CUBE,          "Object", MeshElementKind::Object },
            { ICON_FA_CIRCLE_DOT,    "Vertex", MeshElementKind::Vertex },
            { ICON_FA_GRIP_LINES,    "Edge",   MeshElementKind::Edge   },
            { ICON_FA_VECTOR_SQUARE, "Face",   MeshElementKind::Face   },
        };
        bool firstMode = true;
        for (const ModeEntry& mode : kModes)
        {
            if (!firstMode)
                ImGui::SameLine();
            firstMode = false;
            const bool active = MeshEdit.GetElementKind() == mode.Kind;
            if (ToolButton(mode.Name, mode.Icon, mode.Name, active, buttonSize))
                MeshEdit.SetElementKind(mode.Kind);
        }

        Divider(buttonSize);

        // Grid snap toggle + spacing — drives the shared GridSettings, so picking,
        // manipulators and brush-create all honor it.
        if (ToolButton("snap", ICON_FA_MAGNET,
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
