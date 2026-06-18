#include "EditorStatusBar.h"

#include "EditorUiSkin.h"
#include "EditorUiStyle.h"
#include "fonts/IconsFontAwesome6.h"

#include "../selection/SelectionService.h"
#include "../tools/ITool.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/GridSettings.h"
#include "../viewport/ViewportLayout.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <ctime>

EditorStatusBar::EditorStatusBar(ToolRegistry& tools, ViewportLayout& layout, SelectionService& selection,
                                 const GridSettings& grid)
    : Tools(tools)
    , Layout(layout)
    , Selection(selection)
    , Grid(grid)
{
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
        EditorUiSkin::Band(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                           ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                           EditorUi::HeaderBg);
        if (ImGui::BeginMenuBar())
        {
            const ITool* tool = Tools.GetActiveTool();
            ImGui::Text(ICON_FA_ARROW_POINTER "  %s", tool ? tool->GetDisplayName().data() : "—");

            ImGui::Separator();
            const std::size_t count = Selection.GetSelection().size();
            ImGui::Text("%zu selected", count);

            if (const EditorViewport* active = Layout.Active())
            {
                ImGui::Separator();
                ImGui::Text("%s", active->GetDisplayLabel());
            }
            ImGui::Separator();
            ImGui::Text(ICON_FA_BORDER_ALL "  grid %g%s",
                        Grid.Spacing, Grid.SnapEnabled ? "" : " (snap off)");

            // Wall clock, right-aligned.
            std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            char clock[16];
            std::strftime(clock, sizeof(clock), ICON_FA_CLOCK "  %H:%M", &tm);
            const float clockWidth = ImGui::CalcTextSize(clock).x;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > clockWidth)
                ImGui::SameLine(ImGui::GetCursorPosX() + avail - clockWidth);
            ImGui::TextColored(EditorUi::TextDim, "%s", clock);

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}
