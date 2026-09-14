#include "WorkspaceBar.h"

#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeHeader.h"
#include "ui/chrome/PanelStyle.h"
#include "ui/chrome/ChromeControls.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <algorithm>
#include <string>

namespace
{
// The plate is the primary viewport's header plate; a hairline of chassis
// above and below seats it under the caption the way it sat in the panel.
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

float WorkspaceBar::GroupWidth(float buttonSize) const
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float width = buttonSize; // the profile chevron, always
    if (Play.RunCook)
        width += buttonSize + 1.0f;
    if (Play.Play)
        width += spacing + buttonSize;
    if (Play.Stop)
        width += spacing + buttonSize;
    return width;
}

void WorkspaceBar::Draw()
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
        DrawRow(ImGui::GetWindowDrawList(), mn, ImVec2(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y));
    }
    ImGui::End();
}

void WorkspaceBar::DrawRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx)
{
    const float buttonSize = EditorChrome::BarButtonSize();
    const bool playing = Play.IsPlaying && Play.IsPlaying();
    const bool cooking = Play.IsCooking && Play.IsCooking();

    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(EditorUi::ChassisBg));
    const float inset = std::max(0.0f, (mx.y - mn.y - PlateHeight()) * 0.5f);
    // Untitled and unruled: the cap, the plate, and the control region.
    const EditorChrome::HeaderRowSpec spec{
        .Style = kPlate,
        .ReservedControlWidth = GroupWidth(buttonSize) + EditorUi::Px(EditorUi::Metrics.ModulePad * 2.0f),
        .Rule = false,
    };
    const EditorChrome::HeaderRegions regions = EditorChrome::DrawHeaderRow(
        dl, ImVec2(mn.x, mn.y + inset), ImVec2(mx.x, mx.y - inset), {}, EditorUi::TextRole::PanelTitle,
        EditorChrome::HeaderState{ .Focused = playing || cooking }, spec);
    if (!regions.HasControl)
        return;
    // The controls on the control region's centreline.
    const float laneY = regions.ControlMin.y + std::max(0.0f, (regions.ControlMax.y - regions.ControlMin.y - buttonSize) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(regions.ControlMin.x + EditorUi::Px(EditorUi::Metrics.ModulePad), laneY));
    DrawGroup(buttonSize);
}

void WorkspaceBar::DrawGroup(float buttonSize)
{
    const bool playing = Play.IsPlaying && Play.IsPlaying();
    const bool cooking = Play.IsCooking && Play.IsCooking();
    // The transport bay lights while a session runs or a cook is in flight.
    EditorChrome::ModuleScope module("transport");
    module.SetActive(playing || cooking);

    if (Play.RunCook)
    {
        std::string tooltip = cooking ? "Cancel cook" : "Cook selected profile";
        if (Play.CookStatus)
        {
            const std::string status = Play.CookStatus();
            if (!status.empty())
                tooltip += "\n" + status;
        }
        if (EditorChrome::ToolButton("cook", cooking ? IconId::Cancel : IconId::Hammer,
                       tooltip.c_str(), cooking, buttonSize))
        {
            if (cooking && Play.CancelCook)
                Play.CancelCook();
            else
                Play.RunCook();
        }
        // The chevron is the cook button's split half: a hairline apart.
        ImGui::SameLine(0.0f, 1.0f);
    }
    if (EditorChrome::IconButton("##cook_profiles", IconId::ChevronDown, buttonSize, EditorChrome::ButtonTone::Normal))
        ImGui::OpenPopup("##cook_profile_menu");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cook profiles");
    if (ImGui::BeginPopup("##cook_profile_menu"))
    {
        const std::string selected = Play.SelectedProfileId
            ? Play.SelectedProfileId() : std::string{};
        if (Play.Profiles)
            for (const PlayControls::ProfileChoice& profile : Play.Profiles())
            {
                if (ImGui::MenuItem(profile.Name.c_str(), nullptr,
                                    profile.Id == selected) && Play.SelectProfile)
                    Play.SelectProfile(profile.Id);
            }
        ImGui::Separator();
        if (ImGui::MenuItem("Rebuild selected profile", nullptr, false,
                            bool(Play.RebuildCook) && !cooking))
            Play.RebuildCook();
        if (ImGui::MenuItem("Edit profiles...", nullptr, false,
                            bool(Play.OpenProfiles)))
            Play.OpenProfiles();
        ImGui::EndPopup();
    }
    if (Play.Play)
    {
        ImGui::SameLine();
        if (EditorChrome::ToolButton("play", IconId::Play, playing ? "Playing" : "Play (PIE)", playing, buttonSize))
            Play.Play();
    }
    if (Play.Stop)
    {
        ImGui::SameLine();
        if (EditorChrome::ToolButton("stop", IconId::Stop, "Stop", false, buttonSize) && playing)
            Play.Stop();
    }
}
