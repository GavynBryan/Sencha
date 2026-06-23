#include "EditorToolbar.h"

#include "EditorUiSkin.h"
#include "EditorUiStyle.h"
#include "fonts/IconsFontAwesome6.h"

#include "../meshedit/MeshElementKind.h"
#include "../meshedit/MeshElementKindTraits.h"
#include "../meshedit/MeshEditService.h"
#include "../document/BrushCreationSettings.h"
#include "../tools/ITool.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/GridSettings.h"

#include <algorithm>
#include <array>

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

EditorToolbar::EditorToolbar(ToolRegistry& tools, MeshEditService& meshEdit, GridSettings& grid,
                             BrushCreationSettings& brushCreate)
    : Tools(tools)
    , MeshEdit(meshEdit)
    , Grid(grid)
    , BrushCreate(brushCreate)
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

        // Brush create sub-mode (Cube/Plane/Cylinder): drives BrushCreationSettings.
        // Only meaningful while the Brush tool is active, so it shows only then.
        const ITool* activeTool = Tools.GetActiveTool();
        if (activeTool != nullptr && activeTool->GetId() == "brush")
        {
            Divider(buttonSize);

            struct PrimitiveButton { BrushPrimitive Kind; const char* Icon; const char* Label; };
            static constexpr std::array<PrimitiveButton, 3> kPrimitives = {{
                { BrushPrimitive::Box, ICON_FA_CUBE, "Cube" },
                { BrushPrimitive::Plane, ICON_FA_SQUARE, "Plane" },
                { BrushPrimitive::Cylinder, ICON_FA_CIRCLE, "Cylinder" },
            }};
            bool firstPrim = true;
            for (const PrimitiveButton& prim : kPrimitives)
            {
                if (!firstPrim)
                    ImGui::SameLine();
                firstPrim = false;
                const bool active = BrushCreate.ActivePrimitive == prim.Kind;
                if (ToolButton(prim.Label, prim.Icon, prim.Label, active, buttonSize))
                    BrushCreate.ActivePrimitive = prim.Kind;
            }

            if (BrushCreate.ActivePrimitive == BrushPrimitive::Cylinder)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(72.0f);
                if (ImGui::DragInt("##cylsides", &BrushCreate.CylinderSides, 0.25f, 3, 64, "%d sides"))
                    BrushCreate.CylinderSides = std::clamp(BrushCreate.CylinderSides, 3, 64);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cylinder sides");
            }
        }

        Divider(buttonSize);

        // Mesh element mode (Object/Vertex/Edge/Face): drives MeshEditService.
        // Order + labels come from the shared MeshElementKind traits; only the
        // icon glyph is a UI-local presentation choice (keyed by kind).
        static constexpr std::array<const char*, MeshElementKindCount> kModeIcons = {
            ICON_FA_CUBE,          // Object
            ICON_FA_CIRCLE_DOT,    // Vertex
            ICON_FA_GRIP_LINES,    // Edge
            ICON_FA_VECTOR_SQUARE, // Face
        };
        bool firstMode = true;
        for (MeshElementKind kind : AllMeshElementKinds())
        {
            if (!firstMode)
                ImGui::SameLine();
            firstMode = false;
            const char* label = Traits(kind).Label;
            const bool active = MeshEdit.GetElementKind() == kind;
            if (ToolButton(label, kModeIcons[static_cast<std::size_t>(kind)], label, active, buttonSize))
                MeshEdit.SetElementKind(kind);
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

        // Author -> cook -> play loop. Cook bakes the live level; Play launches it
        // out-of-process (PIE); Stop ends the session. Play flips to a lit state
        // while a session runs.
        const bool playing = Play.IsPlaying && Play.IsPlaying();
        Divider(buttonSize);

        if (Play.Cook)
        {
            if (ToolButton("cook", ICON_FA_HAMMER, "Cook level", false, buttonSize))
                Play.Cook();
        }
        ImGui::SameLine();
        if (Play.Play)
        {
            if (ToolButton("play", ICON_FA_PLAY, playing ? "Playing" : "Play (PIE)", playing, buttonSize))
                Play.Play();
        }
        ImGui::SameLine();
        if (Play.Stop)
        {
            if (ToolButton("stop", ICON_FA_STOP, "Stop", false, buttonSize) && playing)
                Play.Stop();
        }
    }
    ImGui::End();
}
