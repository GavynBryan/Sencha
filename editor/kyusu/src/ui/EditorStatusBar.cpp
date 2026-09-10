#include "EditorStatusBar.h"

#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeDecor.h"
#include "fonts/IconsFontAwesome6.h"

#include "editmodes/ManipulatorSession.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElementKindTraits.h"
#include "selection/SelectionService.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "viewport/EditorViewport.h"
#include "viewport/GridSettings.h"
#include "viewport/ViewportLayout.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <ctime>
#include <cstdio>

EditorStatusBar::EditorStatusBar(std::function<ToolRegistry*()> tools,
                                 std::function<const ManipulatorSession*()> manipulators,
                                 ViewportLayout& layout, SelectionService& selection,
                                 const GridSettings& grid, MeshEditService& meshEdit)
    : ToolsResolver(std::move(tools))
    , ManipulatorsResolver(std::move(manipulators))
    , Layout(layout)
    , Selection(selection)
    , Grid(grid)
    , MeshEdit(meshEdit)
{
}

ToolRegistry& EditorStatusBar::Tools() const
{
    return *ToolsResolver();
}

const ManipulatorSession& EditorStatusBar::Manipulators() const
{
    return *ManipulatorsResolver();
}

namespace
{
const char* TransformModeLabel(TransformMode mode)
{
    switch (mode)
    {
    case TransformMode::Resize: return "Resize";
    case TransformMode::Move:   return "Move";
    case TransformMode::Rotate: return "Rotate";
    case TransformMode::Scale:  return "Scale";
    }
    return "?";
}
}

void EditorStatusBar::Draw()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float barHeight = ImGui::GetFrameHeight();

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_MenuBar;

    if (ImGui::BeginViewportSideBar("##EditorStatusBar", viewport, ImGuiDir_Down, barHeight, flags))
    {
        EditorChrome::BarBackdrop(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                         ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                                  EditorChrome::BarEdge::Top);
        if (ImGui::BeginMenuBar())
        {
            const ITool* tool = Tools().GetActiveTool();
            EditorChrome::Readout("TOOL", tool ? tool->GetDisplayName().data() : "—");
            EditorChrome::Divider();
            EditorChrome::Readout("MODE", Traits(MeshEdit.GetElementKind()).Label);
            EditorChrome::Divider();
            char gizmo[64];
            std::snprintf(gizmo, sizeof(gizmo), "%s (%s)", TransformModeLabel(Manipulators().EffectiveMode()),
                           TransformSpaceLabel(Manipulators().GetTransformSpace()));
            EditorChrome::Readout("GIZMO", gizmo);
            EditorChrome::Divider();
            const std::size_t count = Selection.GetSelection().size();
            char selected[32];
            std::snprintf(selected, sizeof(selected), "%zu", count);
            EditorChrome::Readout("SEL", selected, count > 0 ? EditorChrome::LedState::On : EditorChrome::LedState::Off);
            EditorChrome::Divider();
            const EditorViewport* active = Layout.Active();
            EditorChrome::Readout("VIEW", active ? active->GetDisplayLabel() : "—");
            EditorChrome::Divider();
            char grid[48];
            std::snprintf(grid, sizeof(grid), "%g%s", Grid.Spacing, Grid.HasCustomFrame() ? " [custom]" : "");
            EditorChrome::Readout("GRID", grid, Grid.SnapEnabled ? EditorChrome::LedState::On : EditorChrome::LedState::Alert);

            // Wall clock, right-aligned.
            std::time_t now = std::time(nullptr);
            std::tm tm{};
            // The reentrant local-time call is spelled differently per platform,
            // and the two take their arguments in opposite orders.
#if defined(_WIN32)
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            char clock[16];
            std::strftime(clock, sizeof(clock), ICON_FA_CLOCK "  %H:%M", &tm);
            // The tagline sits before the clock, both right-aligned; the
            // tagline yields first when the bar is narrow.
            const std::string_view tagline = EditorChrome::DecorText(EditorChrome::DecorSlot::StatusTagline);
            const float clockWidth = ImGui::CalcTextSize(clock).x;
            const float taglineWidth = tagline.empty() ? 0.0f
                : EditorUi::MeasureRoleText(EditorUi::TextRole::Status, tagline).x + EditorUi::Px(18.0f);
            const float avail = ImGui::GetContentRegionAvail().x;
            if (!tagline.empty() && avail > clockWidth + taglineWidth)
            {
                ImGui::SameLine(ImGui::GetCursorPosX() + avail - clockWidth - taglineWidth);
                EditorUi::RoleLabel(EditorUi::TextRole::Status, tagline);
                ImGui::SameLine(0.0f, EditorUi::Px(18.0f));
            }
            else if (avail > clockWidth)
            {
                ImGui::SameLine(ImGui::GetCursorPosX() + avail - clockWidth);
            }
            ImGui::TextColored(EditorUi::TextDim, "%s", clock);

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}
