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
const UiActionId kHold = UiActionIdAt(8);
const UiActionId kRelease = UiActionIdAt(9);

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
                     "profiles_delete", "profiles_validate", "profiles_save",     "profiles_close",
                     "profiles_hold",   "profiles_release" };
    return desc;
}

std::size_t IndexArgument(const UiAction& action)
{
    return static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arg(0).AsInt()));
}
} // namespace

CookProfilesModal::CookProfilesModal(UiService& ui, ProjectDescriptor& project)
    : Ui(ui)
    , Profiles(project)
    , Screen(ui, Describe)
{
}

CookProfilesModal::~CookProfilesModal()
{
    Close();
}

void CookProfilesModal::MoveTo(UiSurfaceId surface)
{
    if (surface == Screen.Surface())
        return;
    Close();
    Screen.MoveTo(surface);
}

void CookProfilesModal::Open()
{
    if (Screen.IsOpen() || !Screen.Open())
        return;
    Sheet.Drop();
    Publish();
}

void CookProfilesModal::Close()
{
    Screen.Close();
}

void CookProfilesModal::Update()
{
    if (!Screen.IsOpen())
        return;
    HandleActions();
    Publish();
}

void CookProfilesModal::HandleActions()
{
    for (const UiAction& action : Ui.DrainActions(Screen.Handle()))
    {
        if (action.Id == kSelect)
            Profiles.Select(IndexArgument(action));
        else if (action.Id == kSet && action.Arguments.size() > 1)
            (void)Sheet.SetFromControl(Profiles, IndexArgument(action), action.Arg(1));
        else if (action.Id == kHold)
            Sheet.Hold(IndexArgument(action));
        else if (action.Id == kRelease)
        {
            const std::size_t row = IndexArgument(action);
            const std::vector<UiRow> shown = Ui.GetRows(Screen.Handle(), kSettings);
            (void)Sheet.Release(Profiles, row, row < shown.size() ? std::string_view(shown[row].Value) : std::string_view{});
        }
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
    if (!Screen.IsOpen())
        return;
    std::vector<std::string> names;
    for (const CookProfile& profile : Profiles.Profiles())
        names.push_back(profile.Name);
    (void)Ui.SetArray(Screen.Handle(), kProfiles, names);
    (void)Ui.SetValue(Screen.Handle(), kSelectedIndex, UiValue(static_cast<std::int64_t>(Profiles.Selected())));
    (void)Ui.SetValue(Screen.Handle(), kStatus, UiValue(Profiles.Status()));
    (void)Ui.SetValue(Screen.Handle(), kDirty, UiValue(Profiles.Changed()));
    (void)Ui.SetValue(Screen.Handle(), kBuiltIn, UiValue(Profiles.SelectedIsBuiltIn()));
    if (!Sheet.Holding())
        (void)Ui.SetRows(Screen.Handle(), kSettings, Sheet.Present(Profiles));
}
