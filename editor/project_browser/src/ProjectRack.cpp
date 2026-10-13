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

} // namespace

ProjectRack::ProjectRack(UiService& ui)
    : Ui(ui)
    , Screen(ui, Describe)
{
}

ProjectRack::~ProjectRack()
{
    Close();
}

void ProjectRack::MoveTo(UiSurfaceId surface)
{
    if (surface == Screen.Surface())
        return;
    Close();
    Screen.MoveTo(surface);
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
    if (Screen.IsOpen() || !Screen.Open())
        return;
    Published = false;
}

void ProjectRack::Close()
{
    Screen.Close();
}

std::vector<RackRequest> ProjectRack::Update(const ProjectCatalog& catalog, const std::string& selected, bool creating)
{
    std::vector<RackRequest> requests;
    if (!Screen.IsOpen())
        return requests;
    for (const UiAction& action : Ui.DrainActions(Screen.Handle()))
    {
        if (action.Id == kPick)
            requests.push_back({ RackChoice::Pick, std::string(action.Arg(0).AsString()) });
        else if (action.Id == kOpen)
            requests.push_back({ RackChoice::Open, std::string(action.Arg(0).AsString()) });
        else if (action.Id == kForget)
            requests.push_back({ RackChoice::Forget, std::string(action.Arg(0).AsString()) });
        else if (action.Id == kNew)
            requests.push_back({ RackChoice::New });
        else if (action.Id == kBrowse)
            requests.push_back({ RackChoice::Browse });
    }

    (void)Ui.SetValue(Screen.Handle(), kCreating, UiValue(creating));
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
    (void)Ui.SetRows(Screen.Handle(), kProjects, rows);
    Shown = entries;
    ShownSelection = selected;
    Published = true;
    return requests;
}
