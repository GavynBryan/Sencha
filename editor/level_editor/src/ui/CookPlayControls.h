#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The author -> cook -> play loop as one module of buttons (Cook with its
// profile menu, Play, Stop), driven by callbacks so it knows no project.
class CookPlayControls
{
public:
    struct ProfileChoice
    {
        std::string Id;
        std::string Name;
        bool BuiltIn = false;
    };

    struct Actions
    {
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

    explicit CookPlayControls(Actions actions)
        : Play(std::move(actions))
    {
    }

    // The buttons the wired callbacks put on the lane and the spacing between
    // them: what a host placing the module needs before it is drawn.
    [[nodiscard]] float Width(float buttonSize) const;
    // Drawn at the cursor.
    void Draw(float buttonSize);
    // A session is running or a cook is in flight.
    [[nodiscard]] bool IsBusy() const;

private:
    Actions Play;
};
