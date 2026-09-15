#pragma once

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The bar under the caption: the primary viewport's header plate spanning
// the window, bare of any title, carrying the author -> cook -> play loop
// (Cook with its profile menu, Play, Stop) in its control region at the
// right; the run to its left is where workspace tabs will mount. The plate
// lights while a cook or a session is running. The loop is driven by
// callbacks the host supplies so the bar stays free of project and PIE
// dependencies. Fixed app chrome: Draw opens a viewport side bar of its own
// height, so the work area the panels get already excludes it; DrawRow is
// the plate over any rect.
class WorkspaceBar
{
public:

    struct PlayControls
    {
        struct ProfileChoice
        {
            std::string Id;
            std::string Name;
            bool BuiltIn = false;
        };

        std::function<void()> RunCook;
        std::function<void()> CancelCook;
        std::function<void()> RebuildCook;
        std::function<bool()> IsCooking;
        std::function<std::vector<ProfileChoice>()> Profiles;
        std::function<std::string()> SelectedProfileId;
        std::function<void(std::string_view)> SelectProfile;
        std::function<void()> OpenProfiles;
        // The authored cook-profile workflow, offered beside the ImGui panel
        // while both exist.
        std::function<void()> OpenAuthoredProfiles;
        std::function<std::string()> CookStatus;
        std::function<void()> Play;
        std::function<void()> Stop;
        std::function<bool()> IsPlaying;
    };

    void SetPlayControls(PlayControls controls) { Play = std::move(controls); }

    // The bar as app chrome, reserving its own space below the caption.
    void Draw();
    // The plate painted over [mn, mx], the module in its control region.
    void DrawRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx);
    [[nodiscard]] static float RowHeight();

private:
    void DrawGroup(float buttonSize);
    // The buttons the wired callbacks put on the lane, and the spacing between
    // them: what centring needs before the group is drawn.
    [[nodiscard]] float GroupWidth(float buttonSize) const;

    PlayControls Play;
};
