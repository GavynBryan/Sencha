#include "ProjectRack.h"

#include <ui/UiService.h>

#include <filesystem>

namespace
{
const UiModelRowsId kProjects = UiRowsIdAt(0);
const UiModelPropertyId kCreating = UiPropertyIdAt(0);

const UiActionId kPick = UiActionIdAt(0);
const UiActionId kOpen = UiActionIdAt(1);
const UiActionId kForget = UiActionIdAt(2);
const UiActionId kNew = UiActionIdAt(3);
const UiActionId kBrowse = UiActionIdAt(4);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://project_rack.rml";
    desc.ModelName = "project_rack";
    desc.Properties = { UiModelProperty{ "creating", UiValue(false) } };
    desc.RowLists = { "projects" };
    desc.Actions = { "rack_pick", "rack_open", "rack_forget", "rack_new", "rack_browse" };
    return desc;
}

std::string PathArgument(const UiAction& action)
{
    return action.Arguments.empty() ? std::string{} : std::string(action.Arguments[0].AsString());
}
} // namespace

ProjectRack::~ProjectRack()
{
    Close();
}

void ProjectRack::MoveTo(UiSurfaceId surface)
{
    if (surface == Surface)
        return;
    Close();
    Surface = surface;
    if (Visible)
        Open();
}

void ProjectRack::SetVisible(bool visible)
{
    Visible = visible;
    if (visible)
        Open();
    else
        Close();
}

void ProjectRack::Open()
{
    if (Screen.IsValid() || !Surface.IsValid())
        return;
    Screen = Ui.OpenScreen(Surface, Describe());
    Published = false;
}

void ProjectRack::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}

std::vector<RackRequest> ProjectRack::Update(const ProjectCatalog& catalog, const std::string& selected, bool creating)
{
    std::vector<RackRequest> requests;
    if (!Screen.IsValid())
        return requests;
    for (const UiAction& action : Ui.DrainActions(Screen))
    {
        if (action.Id == kPick)
            requests.push_back({ RackChoice::Pick, PathArgument(action) });
        else if (action.Id == kOpen)
            requests.push_back({ RackChoice::Open, PathArgument(action) });
        else if (action.Id == kForget)
            requests.push_back({ RackChoice::Forget, PathArgument(action) });
        else if (action.Id == kNew)
            requests.push_back({ RackChoice::New });
        else if (action.Id == kBrowse)
            requests.push_back({ RackChoice::Browse });
    }

    (void)Ui.SetValue(Screen, kCreating, UiValue(creating));
    const std::vector<ProjectCatalogEntry>& entries = catalog.Entries();
    const bool same = Published && ShownSelection == selected && Shown.size() == entries.size()
        && std::equal(Shown.begin(), Shown.end(), entries.begin(), [](const auto& a, const auto& b) {
               return a.Path == b.Path && a.Name == b.Name;
           });
    if (same)
        return requests;
    std::vector<UiRow> rows;
    rows.reserve(entries.size());
    for (const ProjectCatalogEntry& entry : entries)
    {
        std::error_code ec;
        const std::filesystem::path path(entry.Path);
        UiRow row{ entry.Name.empty() ? path.parent_path().filename().string() : entry.Name, entry.Path,
                   std::filesystem::exists(path, ec) ? std::string{} : std::string("Missing") };
        row.Key = entry.Path;
        row.Selected = entry.Path == selected;
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kProjects, rows);
    Shown = entries;
    ShownSelection = selected;
    Published = true;
    return requests;
}
