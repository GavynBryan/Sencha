#include "shell/ExplorerPanel.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "icons/IconNames.h"
#include "shell/Shelf.h"
#include "ui/ScreenModel.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

#include <ui/UiAction.h>
#include <ui/UiService.h>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
// How many items an explorer shows at once, and asks its workspace for.
constexpr std::size_t kPage = 40;

const char* ShapeName(ExplorerShape shape)
{
    return shape == ExplorerShape::Outline ? "outline" : "pictures";
}
} // namespace

ExplorerPanel::ExplorerPanel(ExplorerPanelHost host)
    : Host(std::move(host))
{
}

void ExplorerPanel::Declare(ScreenModel& model)
{
    IdProperty = model.Property("explorer_id", UiValue(std::string{}));
    Title = model.Property("explorer_title", UiValue(std::string{}));
    Shape = model.Property("explorer_shape", UiValue(std::string{}));
    Range = model.Property("explorer_range", UiValue(std::string{}));
    Empty = model.Property("explorer_empty", UiValue(std::string{}));
    Earlier = model.Property("explorer_earlier", UiValue(false));
    Later = model.Property("explorer_later", UiValue(false));
    Items = model.Rows("explorer_items");
    Verbs = model.Rows("explorer_verbs");
    Explore = model.Action("shell_explore");
    FilterAction = model.Action("shell_explorer_filter");
    Page = model.Action("shell_explorer_page");
    PickAction = model.Action("shell_explorer_pick");
    Commit = model.Action("shell_explorer_commit");
    CloseAction = model.Action("shell_explorer_close");
    OpenAction = model.Action("shell_explorer_open");
    Mark = model.Action("shell_explorer_mark");
}

void ExplorerPanel::Handle(std::span<const UiAction> actions)
{
    // A press on a row's arrow or mark reaches the row too, later in the same
    // drain: it did what it was for, so the row does not also pick.
    std::string handledInRow;
    for (const UiAction& action : actions)
    {
        const std::string argument(action.Arg(0).AsString());
        if (action.Id == Explore)
        {
            if (argument == ShownId)
                Close();
            else
                Show(argument);
        }
        else if (action.Id == FilterAction)
        {
            Filter = argument;
            First = 0;
        }
        else if (action.Id == Page)
        {
            const std::int64_t step = action.Arg(0).AsInt();
            if (step < 0)
                First -= std::min(First, kPage);
            else if (step > 0)
                First += kPage;
        }
        else if (action.Id == CloseAction)
            Close();
        else if (action.Id == PickAction || action.Id == Commit || action.Id == OpenAction || action.Id == Mark)
        {
            IWorkspace* workspace = Host.Workspaces.ActiveIn(Host.Presentation);
            if (workspace == nullptr)
                continue;
            if (action.Id == OpenAction || action.Id == Mark)
            {
                Choose(*workspace, argument, action.Id == OpenAction ? ExplorerChoice::Open : ExplorerChoice::Mark,
                       static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arg(1).AsInt())));
                handledInRow = argument;
                continue;
            }
            if (action.Id == PickAction && argument == handledInRow)
                continue;
            const ExplorerChoice choice = action.Id == Commit       ? ExplorerChoice::Commit
                                        : action.Arg(1).AsBool() ? ExplorerChoice::Extend
                                        : action.Arg(2).AsBool() ? ExplorerChoice::Toggle
                                                                 : ExplorerChoice::Pick;
            Choose(*workspace, argument, choice, 0);
        }
    }
}

void ExplorerPanel::Choose(IWorkspace& workspace, const std::string& item, ExplorerChoice choice, std::size_t mark)
{
    workspace.ChooseInExplorer(ShownId, item, choice, mark);
    // Putting an item to use is what an explorer of pictures was open for; an
    // outline stays, since it is where the structure is worked.
    if (choice != ExplorerChoice::Commit || OpenTransient == 0)
        return;
    const std::vector<WorkspaceExplorerInfo> explorers = workspace.Explorers();
    const auto shown = std::ranges::find(explorers, ShownId, &WorkspaceExplorerInfo::Id);
    if (shown != explorers.end() && shown->Shape == ExplorerShape::Pictures)
        Close();
}

void ExplorerPanel::Show(std::string id)
{
    Close();
    if (id.empty())
        return;
    // The project's files are chosen from the shelf rather than a tray.
    if (IWorkspace* active = Host.Workspaces.ActiveIn(Host.Presentation))
        for (const WorkspaceExplorerInfo& info : active->Explorers())
            if (info.Id == id && info.Shape == ExplorerShape::Files)
            {
                ChoosingFor = id;
                Host.Assets.Pick({ .Title = info.Title,
                                   .Accepts = info.Accepts,
                                   .Current = info.Current,
                                   .Withheld = info.Withheld,
                                   .Choose = [this, id](const std::string& path, bool commit) {
                                       if (IWorkspace* workspace = Host.Workspaces.ActiveIn(Host.Presentation))
                                           workspace->ChooseInExplorer(
                                               id, path, commit ? ExplorerChoice::Commit : ExplorerChoice::Pick, 0);
                                   } });
                return;
            }
    ShownId = std::move(id);
    First = 0;
    if (Host.BringToFront)
        Host.BringToFront();
    OpenTransient = Host.Transients.Open();
    if (Host.TakeEscape)
        Host.TakeEscape();
}

void ExplorerPanel::Close()
{
    if (OpenTransient != 0)
        Host.Transients.Close(OpenTransient);
}

bool ExplorerPanel::Closed(TransientSurfaces::Id id)
{
    if (id == 0 || id != OpenTransient)
        return false;
    OpenTransient = 0;
    ShownId.clear();
    return true;
}

void ExplorerPanel::Reveal(const std::string& id, const std::string& key)
{
    if (ShownId != id || OpenTransient == 0)
        Show(id);
    if (Host.BringToFront)
        Host.BringToFront();
    if (IWorkspace* workspace = Host.Workspaces.ActiveIn(Host.Presentation); workspace != nullptr && !key.empty())
        workspace->ChooseInExplorer(id, key, ExplorerChoice::Reveal, 0);
}

void ExplorerPanel::Claim(const std::vector<WorkspaceExplorerInfo>& explorers)
{
    std::vector<std::string> actions;
    for (const WorkspaceExplorerInfo& explorer : explorers)
        actions.push_back(explorer.Action);
    if (actions == Claimed)
        return;
    for (const std::string& action : Claimed)
        Host.WindowActions.Release(action);
    for (const WorkspaceExplorerInfo& explorer : explorers)
        Host.WindowActions.Claim(explorer.Action, ActionHandler{ .Invoke = [this, id = explorer.Id] {
                                     if (id == ShownId)
                                         Close();
                                     else if (!Host.Assets.Choosing().empty() && id == ChoosingFor)
                                         Host.Assets.Hide();
                                     else
                                         Show(id);
                                 } });
    Claimed = std::move(actions);
    // What was open belongs to the workspace that was showing.
    if (!ShownId.empty() && std::ranges::none_of(explorers, [&](const WorkspaceExplorerInfo& e) { return e.Id == ShownId; }))
        Close();
}

void ExplorerPanel::Publish(IWorkspace* workspace)
{
    UiService& ui = Host.Ui;
    const std::vector<WorkspaceExplorerInfo> explorers =
        workspace != nullptr ? workspace->Explorers() : std::vector<WorkspaceExplorerInfo>{};
    Claim(explorers);
    const auto shown = std::ranges::find(explorers, ShownId, &WorkspaceExplorerInfo::Id);
    if (shown == explorers.end() || workspace == nullptr)
    {
        Key.clear();
        (void)ui.SetValue(Screen, IdProperty, UiValue(std::string{}));
        return;
    }
    Key = std::format("explorer.{}", ShapeName(shown->Shape));
    ExplorerPage page = workspace->Explore({ .Explorer = ShownId, .Filter = Filter, .First = First, .Count = kPage });
    // A narrower filter can leave the page past the end.
    if (First > 0 && First >= page.Total)
    {
        First = 0;
        page = workspace->Explore({ .Explorer = ShownId, .Filter = Filter, .First = 0, .Count = kPage });
    }
    (void)ui.SetValue(Screen, IdProperty, UiValue(ShownId));
    (void)ui.SetValue(Screen, Title, UiValue(shown->Title));
    (void)ui.SetValue(Screen, Shape, UiValue(std::string(ShapeName(shown->Shape))));
    (void)ui.SetValue(Screen, Range,
                      UiValue(page.Total == 0 ? std::string{}
                                              : std::format("{}-{} of {}", First + 1, First + page.Items.size(),
                                                            page.Total)));
    (void)ui.SetValue(Screen, Empty, UiValue(page.Items.empty() ? page.Empty : std::string{}));
    (void)ui.SetValue(Screen, Earlier, UiValue(First > 0));
    (void)ui.SetValue(Screen, Later, UiValue(First + page.Items.size() < page.Total));

    std::vector<UiRow> items;
    const bool outline = shown->Shape == ExplorerShape::Outline;
    for (const ExplorerItem& item : page.Items)
    {
        // An outline has no pictures: its value is whether the item is open.
        const char* branch = item.Branch == ExplorerBranch::Open     ? "open"
                           : item.Branch == ExplorerBranch::Closed ? "closed"
                                                                   : "";
        UiRow row{ item.Label, outline ? std::string(branch) : item.Image, "" };
        row.Key = item.Key;
        row.Depth = item.Depth;
        row.Selected = item.Current;
        for (const IconId mark : item.Marks)
            row.Parts.emplace_back(IconName(mark));
        items.push_back(std::move(row));
    }
    (void)ui.SetRows(Screen, Items, items);

    // What cannot be done now is not offered, rather than shown greyed.
    std::vector<UiRow> verbs;
    for (const std::string& id : page.Actions)
        if (const ActionDefinition* definition = Host.Catalog.Find(id);
            definition != nullptr && Host.Actions.Query(id).Enabled)
        {
            UiRow verb{ definition->Label, "", "" };
            verb.Key = id;
            verbs.push_back(std::move(verb));
        }
    (void)ui.SetRows(Screen, Verbs, verbs);
}
