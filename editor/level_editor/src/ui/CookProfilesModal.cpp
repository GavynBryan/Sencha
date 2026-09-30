#include "ui/CookProfilesModal.h"

#include "project/CookProfile.h"

#include <ui/UiService.h>

#include <algorithm>

namespace
{
const UiModelArrayId kProfiles = UiArrayIdAt(0);

const UiModelPropertyId kSelectedIndex = UiPropertyIdAt(0);
const UiModelPropertyId kStatus = UiPropertyIdAt(1);
const UiModelPropertyId kDirty = UiPropertyIdAt(2);
const UiModelPropertyId kBuiltIn = UiPropertyIdAt(3);

const UiModelRowsId kSettings = UiRowsIdAt(0);

const UiActionId kSelect = UiActionIdAt(0);
const UiActionId kSet = UiActionIdAt(1);
const UiActionId kFlip = UiActionIdAt(2);
const UiActionId kDuplicate = UiActionIdAt(3);
const UiActionId kDelete = UiActionIdAt(4);
const UiActionId kValidate = UiActionIdAt(5);
const UiActionId kSave = UiActionIdAt(6);
const UiActionId kClose = UiActionIdAt(7);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://cook_profiles.rml";
    desc.ModelName = "cook_profiles";
    desc.Modal = true;
    desc.Arrays = { "profiles" };
    desc.Properties = {
        UiModelProperty{ "selected_index", UiValue(std::int64_t{ 0 }) },
        UiModelProperty{ "status", UiValue(std::string{}) },
        UiModelProperty{ "dirty", UiValue(false) },
        UiModelProperty{ "builtin", UiValue(false) },
    };
    desc.RowLists = { "settings" };
    desc.Actions = { "profiles_select", "profiles_set",      "profiles_flip",     "profiles_duplicate",
                     "profiles_delete", "profiles_validate", "profiles_save",     "profiles_close" };
    return desc;
}

std::size_t IndexArgument(const UiAction& action)
{
    return action.Arguments.empty() ? 0 : static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arguments[0].AsInt()));
}
} // namespace

CookProfilesModal::CookProfilesModal(UiService& ui, ProjectDescriptor& project)
    : Ui(ui)
    , Profiles(project)
{
}

CookProfilesModal::~CookProfilesModal()
{
    Close();
}

void CookProfilesModal::MoveTo(UiSurfaceId surface)
{
    if (surface == Surface)
        return;
    Close();
    Surface = surface;
}

void CookProfilesModal::Open()
{
    if (Screen.IsValid() || !Surface.IsValid())
        return;
    Screen = Ui.OpenScreen(Surface, Describe());
    Publish();
}

void CookProfilesModal::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}

void CookProfilesModal::Update()
{
    if (!Screen.IsValid())
        return;
    HandleActions();
    Publish();
}

void CookProfilesModal::HandleActions()
{
    for (const UiAction& action : Ui.DrainActions(Screen))
    {
        if (action.Id == kSelect)
            Profiles.Select(IndexArgument(action));
        else if (action.Id == kSet && action.Arguments.size() > 1)
            (void)Sheet.SetFromControl(Profiles, IndexArgument(action), action.Arguments[1]);
        else if (action.Id == kFlip)
            (void)Sheet.Flip(Profiles, IndexArgument(action));
        else if (action.Id == kDuplicate)
            Profiles.Duplicate();
        else if (action.Id == kDelete)
            (void)Profiles.Delete();
        else if (action.Id == kValidate)
            (void)Profiles.Validate();
        else if (action.Id == kSave)
            (void)Profiles.Save();
        else if (action.Id == kClose)
        {
            Close();
            return;
        }
    }
}

void CookProfilesModal::Publish()
{
    if (!Screen.IsValid())
        return;
    std::vector<std::string> names;
    for (const CookProfile& profile : Profiles.Profiles())
        names.push_back(profile.Name);
    (void)Ui.SetArray(Screen, kProfiles, names);
    (void)Ui.SetValue(Screen, kSelectedIndex, UiValue(static_cast<std::int64_t>(Profiles.Selected())));
    (void)Ui.SetValue(Screen, kStatus, UiValue(Profiles.Status()));
    (void)Ui.SetValue(Screen, kDirty, UiValue(Profiles.Changed()));
    (void)Ui.SetValue(Screen, kBuiltIn, UiValue(Profiles.SelectedIsBuiltIn()));
    (void)Ui.SetRows(Screen, kSettings, Sheet.Present(Profiles));
}
