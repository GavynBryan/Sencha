#include "CookPlayControls.h"

#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"

#include <imgui.h>

#include <string>

float CookPlayControls::Width(float buttonSize) const
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

bool CookPlayControls::IsBusy() const
{
    return (Play.IsPlaying && Play.IsPlaying()) || (Play.IsCooking && Play.IsCooking());
}

void CookPlayControls::Draw(float buttonSize)
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
            for (const ProfileChoice& profile : Play.Profiles())
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
        if (ImGui::MenuItem("Edit profiles (authored)...", nullptr, false,
                            bool(Play.OpenAuthoredProfiles)))
            Play.OpenAuthoredProfiles();
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
