#include "WorkspaceBar.h"

#include "EditorUiStyle.h"
#include "WorkspaceView.h"
#include "chrome/ChromeBars.h"
#include "chrome/ChromeControls.h"
#include "chrome/ChromeHeader.h"
#include "chrome/PanelStyle.h"
#include "workspaces/WorkspaceHost.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <algorithm>
#include <string>

namespace
{
// The plate is the primary viewport's header plate; a hairline of chassis
// above and below seats it under the caption.
constexpr PanelStyle kPlate = PanelStyle::ViewportPrimary;

float PlateHeight()
{
    return std::max(EditorChrome::HeaderRowHeight(kPlate), ImGui::GetFrameHeight() + EditorUi::Px(4.0f));
}
}

float WorkspaceBar::RowHeight()
{
    return PlateHeight() + EditorUi::Px(4.0f);
}

void WorkspaceBar::Draw(const WorkspaceBarControls* controls)
{
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
    // The plate owns the bar's geometry, so the window contributes no padding.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::BeginViewportSideBar("##WorkspaceBar", ImGui::GetMainViewport(), ImGuiDir_Up, RowHeight(), flags);
    ImGui::PopStyleVar();
    if (open)
    {
        const ImVec2 mn = ImGui::GetWindowPos();
        DrawRow(ImGui::GetWindowDrawList(), mn,
                ImVec2(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y), controls);
    }
    ImGui::End();
}

void WorkspaceBar::DrawRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const WorkspaceBarControls* controls)
{
    const float buttonSize = EditorChrome::BarButtonSize();
    const bool hasControls = controls != nullptr && controls->Draw && controls->Width;
    const float controlWidth = hasControls ? controls->Width() : 0.0f;
    const bool lit = hasControls && controls->Lit && controls->Lit();

    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(EditorUi::ChassisBg));
    const float inset = std::max(0.0f, (mx.y - mn.y - PlateHeight()) * 0.5f);
    // Untitled and unruled: the cap, the plate, and the control region.
    const EditorChrome::HeaderRowSpec spec{
        .Style = kPlate,
        .ReservedControlWidth = controlWidth + EditorUi::Px(EditorUi::Metrics.ModulePad * 2.0f),
        .Rule = false,
    };
    const EditorChrome::HeaderRegions regions = EditorChrome::DrawHeaderRow(
        dl, ImVec2(mn.x, mn.y + inset), ImVec2(mx.x, mx.y - inset), {}, EditorUi::TextRole::PanelTitle,
        EditorChrome::HeaderState{ .Focused = lit }, spec);

    const float laneTop = mn.y + inset;
    const float laneHeight = std::max(0.0f, (mx.y - inset) - laneTop);
    const float laneY = laneTop + std::max(0.0f, (laneHeight - buttonSize) * 0.5f);
    const float pad = EditorUi::Px(EditorUi::Metrics.ModulePad);
    ImGui::SetCursorScreenPos(ImVec2(mn.x + pad * 3.0f, laneY));
    DrawTabs(buttonSize, regions.HasControl ? regions.ControlMin.x - pad : mx.x - pad);

    if (!hasControls || !regions.HasControl)
        return;
    const float controlY = regions.ControlMin.y
                         + std::max(0.0f, (regions.ControlMax.y - regions.ControlMin.y - buttonSize) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(regions.ControlMin.x + pad, controlY));
    controls->Draw();
}

void WorkspaceBar::DrawTabs(float buttonSize, float right)
{
    EditorChrome::ModuleScope module("workspaces");
    const WorkspaceKind* active = Host.ActiveKindIn(Window);
    const bool mainWindow = Window == Host.MainWindow();
    const float stripBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    for (const WorkspaceHost::Entry& entry : Host.OpenWorkspaces())
    {
        if (entry.Window != Window)
            continue;
        const WorkspaceKind& kind = *entry.Kind;
        ImGui::PushID(kind.Id.c_str());
        const bool isActive = &kind == active;
        if (EditorChrome::Button("tab", kind.DisplayName.c_str(), ImVec2(0.0f, buttonSize),
                                 isActive ? EditorChrome::ButtonTone::Active : EditorChrome::ButtonTone::Normal)
            && !isActive)
        {
            Host.Request({ WorkspaceAction::Activate, kind.Id });
        }
        // Pulled well clear of the strip, a tab leaves for a window of its own.
        if (mainWindow && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)
            && ImGui::GetIO().MousePos.y > stripBottom + RowHeight() * 2.0f)
        {
            Host.Request({ WorkspaceAction::Detach, kind.Id });
            ImGui::ClearActiveID();
        }
        if (ImGui::BeginPopupContextItem("tab_menu"))
        {
            if (ImGui::MenuItem(mainWindow ? "Move to New Window" : "Move to Main Window"))
                Host.Request({ mainWindow ? WorkspaceAction::Detach : WorkspaceAction::Attach, kind.Id });
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0f, 1.0f);
        if (EditorChrome::IconButton("close", IconId::WindowClose, buttonSize, EditorChrome::ButtonTone::Normal))
            Host.Request({ WorkspaceAction::Close, kind.Id });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Close %s", kind.DisplayName.c_str());
        ImGui::PopID();
        ImGui::SameLine();
    }

    // New workspaces open in the main window, so only its strip offers them.
    if (!mainWindow)
    {
        ImGui::NewLine();
        return;
    }
    // Nothing to open means no button: every offered kind is already a tab.
    bool anyClosed = false;
    for (const WorkspaceKind& kind : Host.Kinds())
        anyClosed |= Host.IsOffered(kind.Id) && Host.Find(kind.Id) == nullptr;
    if (!anyClosed || ImGui::GetCursorScreenPos().x + buttonSize > right)
    {
        ImGui::NewLine();
        return;
    }
    if (EditorChrome::IconButton("##open_workspace", IconId::Add, buttonSize, EditorChrome::ButtonTone::Normal))
        ImGui::OpenPopup("##open_workspace_menu");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open a workspace");
    if (ImGui::BeginPopup("##open_workspace_menu"))
    {
        for (const WorkspaceKind& kind : Host.Kinds())
        {
            if (!Host.IsOffered(kind.Id) || Host.Find(kind.Id) != nullptr)
                continue;
            if (ImGui::MenuItem(kind.DisplayName.c_str()))
                Host.Request({ WorkspaceAction::Open, kind.Id });
        }
        ImGui::EndPopup();
    }
}
