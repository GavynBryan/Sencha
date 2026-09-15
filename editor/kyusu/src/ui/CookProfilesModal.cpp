#include "CookProfilesModal.h"

#include "document/CookGraph.h"
#include "project/CookProfile.h"
#include "project/Project.h"

#include <ui/UiService.h>

#include <algorithm>
#include <filesystem>

namespace
{
constexpr std::size_t Index(auto id) { return static_cast<std::size_t>(id); }
} // namespace

CookProfilesModal::CookProfilesModal(UiService& ui, UiSurfaceId surface,
                                     ProjectDescriptor* project)
    : Ui(ui)
    , Project(project)
    , Surface(surface)
{
}

UiScreenDesc CookProfilesModal::Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://cook_profiles.rml";
    desc.ModelName = "cook_profiles";
    desc.Modal = true;

    // Order matches the Property enum. Only the name is editable: everything
    // else is the editor telling the document what to show.
    desc.Arrays = { "profiles", "steps" };
    desc.Properties = {
        UiModelProperty{ "selected_index", UiValue(0) },
        UiModelProperty{ "selected_name", UiValue(std::string{}), true },
        UiModelProperty{ "status", UiValue(std::string{}) },
        UiModelProperty{ "dirty", UiValue(false) },
        UiModelProperty{ "builtin", UiValue(false) },
    };
    desc.Actions = { "profiles_select", "profiles_save",
                     "profiles_validate", "profiles_close" };
    return desc;
}

// Arrays and properties are numbered separately, so the enum above indexes
// whichever list the entry belongs to.
namespace
{
constexpr UiModelArrayId kProfilesArray = UiModelArrayId{ 1 };
constexpr UiModelArrayId kStepsArray = UiModelArrayId{ 2 };

constexpr UiModelPropertyId kSelectedIndex = UiModelPropertyId{ 1 };
constexpr UiModelPropertyId kSelectedName = UiModelPropertyId{ 2 };
constexpr UiModelPropertyId kStatus = UiModelPropertyId{ 3 };
constexpr UiModelPropertyId kDirty = UiModelPropertyId{ 4 };
constexpr UiModelPropertyId kBuiltIn = UiModelPropertyId{ 5 };

constexpr UiActionId kSelect = UiActionId{ 1 };
constexpr UiActionId kSave = UiActionId{ 2 };
constexpr UiActionId kValidate = UiActionId{ 3 };
constexpr UiActionId kClose = UiActionId{ 4 };
} // namespace

std::vector<CookProfile> CookProfilesModal::ResolvedProfiles() const
{
    if (Project == nullptr)
        return {};
    return ResolveCookProfiles(Project->CookProfiles);
}

void CookProfilesModal::Open()
{
    if (Screen.IsValid() || Project == nullptr || !Surface.IsValid())
        return;

    Screen = Ui.OpenScreen(Surface, Describe());
    if (!Screen.IsValid())
        return;

    Selected = 0;
    NameShowing = static_cast<std::size_t>(-1);
    Status.clear();
    Dirty = false;
    Publish();
}

void CookProfilesModal::Close()
{
    if (!Screen.IsValid())
        return;
    // Whatever was being typed goes with it. That is not a loss of work being
    // tolerated -- it is an edit that never left the presentation copy, so
    // there is nothing here to undo.
    Ui.CloseScreen(Screen);
    Screen = {};
}

void CookProfilesModal::Update()
{
    if (!Screen.IsValid())
        return;

    for (const UiAction& action : Ui.DrainActions(Screen))
    {
        if (action.Id == kSelect)
        {
            const std::size_t index = action.Arguments.empty()
                ? 0u
                : static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arguments[0].AsInt()));
            HandleSelect(index);
        }
        else if (action.Id == kSave)
        {
            Save();
        }
        else if (action.Id == kValidate)
        {
            if (ApplyNameEdit())
                Validate();
        }
        else if (action.Id == kClose)
        {
            Close();
            return;
        }
    }

    Publish();
}

void CookProfilesModal::HandleSelect(std::size_t index)
{
    const std::vector<CookProfile> profiles = ResolvedProfiles();
    if (index >= profiles.size() || index == Selected)
        return;

    // Moving off a row takes whatever was typed into the name with it, so the
    // edit is not silently dropped when the user looks at something else. A
    // refusal keeps its explanation; the selection still moves, because
    // trapping someone on a row they cannot edit would be worse.
    if (ApplyNameEdit())
        Status.clear();
    Selected = index;
}

bool CookProfilesModal::ApplyNameEdit()
{
    // Reading the presentation copy is the commit. Until this runs, the typed
    // name exists only in the screen.
    if (Project == nullptr || !Screen.IsValid())
        return true;

    const std::vector<CookProfile> profiles = ResolvedProfiles();
    if (Selected >= profiles.size())
        return true;

    const std::string edited(Ui.GetValue(Screen, kSelectedName).AsString());
    if (edited.empty() || edited == profiles[Selected].Name)
        return true;

    // A built-in is inherited, not owned. Renaming one would mean writing a
    // project-local override, which is a different decision than the one the
    // dialog is offering.
    if (profiles[Selected].BuiltIn)
    {
        Status = "Built-in profiles cannot be renamed.";
        return false;
    }

    const auto owned = std::find_if(Project->CookProfiles.begin(), Project->CookProfiles.end(),
        [&](const CookProfile& profile) { return profile.Id == profiles[Selected].Id; });
    if (owned == Project->CookProfiles.end())
        return true;

    owned->Name = edited;
    Dirty = true;
    // The field already shows this, so a republish would be a no-op that only
    // risks fighting the user's caret.
    NameShowing = Selected;
    return true;
}

void CookProfilesModal::Validate()
{
    Status.clear();
    if (Project == nullptr)
        return;

    // The same rules the ImGui panel applies, because they are the project's
    // rules rather than a panel's.
    std::vector<std::string> ids;
    for (const CookProfile& profile : ResolvedProfiles())
    {
        if (std::find(ids.begin(), ids.end(), profile.Id) != ids.end())
        {
            Status = "Duplicate profile id '" + profile.Id + "'";
            return;
        }
        ids.push_back(profile.Id);

        const ResolvedCookGraph graph = ResolveDocumentCookGraph(profile);
        if (!graph.Valid)
        {
            Status = profile.Name + ": " + graph.Error;
            return;
        }
    }
    Status = "Profiles are valid.";
}

void CookProfilesModal::Save()
{
    if (Project == nullptr)
        return;

    // A refused edit stops the save with its own explanation intact. Carrying
    // on would report "Saved" for a rename that did not happen.
    if (!ApplyNameEdit())
        return;

    Validate();
    // Validate leaves a message either way; only a failure should stop a save,
    // and a failure is anything that is not the all-clear.
    if (Status != "Profiles are valid.")
        return;

    std::string error;
    const std::filesystem::path path =
        std::filesystem::path(Project->Directory) / "project.senchaproj";
    if (!Project->Save(path.string(), &error))
    {
        Status = error;
        return;
    }

    Dirty = false;
    Status = "Saved " + path.generic_string();
}

void CookProfilesModal::Publish()
{
    if (!Screen.IsValid())
        return;

    const std::vector<CookProfile> profiles = ResolvedProfiles();
    if (Selected >= profiles.size())
        Selected = 0;

    std::vector<std::string> names;
    names.reserve(profiles.size());
    for (const CookProfile& profile : profiles)
        names.push_back(profile.BuiltIn ? profile.Name + "  (built-in)" : profile.Name);
    (void)Ui.SetArray(Screen, kProfilesArray, names);

    std::vector<std::string> steps;
    if (Selected < profiles.size())
    {
        steps.reserve(profiles[Selected].TargetSteps.size());
        for (const std::string& step : profiles[Selected].TargetSteps)
            steps.push_back(step);
    }
    (void)Ui.SetArray(Screen, kStepsArray, steps);

    (void)Ui.SetValue(Screen, kSelectedIndex, UiValue(static_cast<std::int64_t>(Selected)));
    (void)Ui.SetValue(Screen, kStatus, UiValue(Status));
    (void)Ui.SetValue(Screen, kDirty, UiValue(Dirty));

    if (Selected < profiles.size())
    {
        (void)Ui.SetValue(Screen, kBuiltIn, UiValue(profiles[Selected].BuiltIn));

        // Refilled when the selection moves, and at no other time. Republishing
        // it every frame would overwrite what somebody is typing between
        // keystrokes; deciding by comparing the shown text against the profile
        // names cannot tell "still editing" from "renamed it to another
        // profile's name", which is why this tracks the selection instead.
        if (NameShowing != Selected)
        {
            (void)Ui.SetValue(Screen, kSelectedName, UiValue(profiles[Selected].Name));
            NameShowing = Selected;
        }
    }
}
