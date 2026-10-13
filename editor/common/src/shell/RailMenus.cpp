#include "shell/RailMenus.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "icons/IconNames.h"
#include "problems/ProblemList.h"
#include "shell/SurfacePlacement.h"
#include "text/TextFilterMatch.h"
#include "ui/DocumentLengths.h"
#include "ui/ScreenModel.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

#include <platform/SdlWindow.h>
#include <ui/UiAction.h>
#include <ui/UiService.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace
{
// Choices beyond this many are worth narrowing by typing.
constexpr std::size_t kChoicesUnfiltered = 6;
// How far a popover stays from its instrument and the window's edges, in pixels.
constexpr float kPopoverGap = 4.0f;
constexpr float kPopoverMargin = 8.0f;
constexpr std::string_view kCategoryPrefix = "category:";

UiRow Heading(std::string label)
{
    UiRow row{ std::move(label), "", "" };
    row.Control = UiRowControl::Section;
    return row;
}

UiRow ActionEntry(const ActionCatalog& catalog, const ActionDefinition& action, bool checked)
{
    UiRow row{ action.Label, catalog.ChordText(action.Id), "" };
    row.Key = action.Id;
    row.Icon = std::string(IconName(action.Icon));
    row.Selected = checked;
    return row;
}
} // namespace

RailMenus::RailMenus(RailMenusHost host)
    : Host(std::move(host))
{
}

void RailMenus::Declare(ScreenModel& model)
{
    PopoverName = model.Property("popover", UiValue(std::string{}));
    X = model.Property("popover_x", UiValue(0.0));
    Rises = model.Property("popover_rises", UiValue(false));
    Top = model.Property("popover_top", UiValue(std::string("auto")));
    Filters = model.Property("popover_filters", UiValue(false));
    Tallest = model.Property("popover_tallest", UiValue(std::string("none")));
    Entries = model.Rows("entries");
    PopoverAction = model.Action("shell_popover");
    ChooseAction = model.Action("shell_choose");
    FilterAction = model.Action("shell_popover_filter");
}

bool RailMenus::Handle(const UiAction& action)
{
    const std::string argument(action.Arg(0).AsString());
    if (action.Id == PopoverAction)
    {
        // Pressing the instrument whose popover is open closes it.
        if (argument == Name())
            Show(Popover::None);
        else
            ShowNamed(argument);
    }
    else if (action.Id == ChooseAction)
        Choose(argument);
    else if (action.Id == FilterAction)
        ChoicesFilter = argument;
    else
        return false;
    return true;
}

void RailMenus::Show(Popover popover, std::string category)
{
    if (OpenTransient != 0)
        Host.Transients.Close(OpenTransient);
    if (popover == Popover::None)
        return;
    Shown = popover;
    Category = popover == Popover::Category ? std::move(category) : std::string{};
    OpenTransient = Host.Transients.Open(1);
    if (Host.TakeEscape)
        Host.TakeEscape();
}

void RailMenus::ShowNamed(std::string_view name)
{
    if (name.starts_with(kCategoryPrefix))
        Show(Popover::Category, std::string(name.substr(kCategoryPrefix.size())));
    else
        Show(name == "hatch"      ? Popover::Hatch
             : name == "tools"    ? Popover::Tools
             : name == "problems" ? Popover::Problems
                                  : Popover::None);
}

void RailMenus::ShowChoices(SheetPoint at, std::string verb, std::vector<std::string> choices,
                            std::vector<std::string> notes)
{
    Show(Popover::Choices);
    ChoicesAt = at;
    ChoicesVerb = std::move(verb);
    ChoicesListed = std::move(choices);
    ChoicesNotes = std::move(notes);
    ChoicesFilter.clear();
}

bool RailMenus::Closed(TransientSurfaces::Id id)
{
    if (id == 0 || id != OpenTransient)
        return false;
    OpenTransient = 0;
    Shown = Popover::None;
    return true;
}

std::string RailMenus::Name() const
{
    switch (Shown)
    {
    case Popover::Hatch:    return "hatch";
    case Popover::Tools:    return "tools";
    case Popover::Category: return std::string(kCategoryPrefix) + Category;
    case Popover::Choices:  return "choices";
    case Popover::Problems: return "problems";
    case Popover::Color:
    case Popover::None:
    default:                return {};
    }
}

void RailMenus::Choose(const std::string& key)
{
    const Popover chosenFrom = Shown;
    Show(Popover::None);
    if (chosenFrom == Popover::Problems)
        Host.RevealProblem(key);
    else if (chosenFrom == Popover::Choices)
        Host.ChooseForSheet(ChoicesAt, ChoicesVerb, key);
    else
        (void)Host.Actions.Invoke(key);
}

std::vector<UiRow> RailMenus::HatchEntries() const
{
    // Everything this window can do right now, by category: the menu, with no
    // menu bar. Categories keep the order their first action was defined in,
    // which puts the application's own File and Edit first.
    std::vector<std::pair<std::string, std::vector<UiRow>>> categories;
    for (const ActionDefinition* action : Host.Catalog.All())
    {
        auto category = std::ranges::find(categories, action->Category,
                                          &std::pair<std::string, std::vector<UiRow>>::first);
        if (category == categories.end())
            category = categories.insert(categories.end(), { action->Category, {} });
        const ActionContextStack::State state = Host.Actions.Query(action->Id);
        if (state.Claimed && state.Enabled)
            category->second.push_back(ActionEntry(Host.Catalog, *action, state.Checked));
    }
    std::vector<UiRow> rows;
    for (auto& [name, entries] : categories)
    {
        if (entries.empty())
            continue;
        rows.push_back(Heading(name));
        for (UiRow& entry : entries)
            rows.push_back(std::move(entry));
    }
    return rows;
}

std::vector<UiRow> RailMenus::CategoryEntries(std::string_view category) const
{
    std::vector<UiRow> rows;
    for (const ActionDefinition* action : Host.Catalog.All())
    {
        if (action->Category != category)
            continue;
        const ActionContextStack::State state = Host.Actions.Query(action->Id);
        if (state.Claimed && state.Enabled)
            rows.push_back(ActionEntry(Host.Catalog, *action, state.Checked));
    }
    return rows;
}

std::vector<UiRow> RailMenus::ToolEntries() const
{
    std::vector<UiRow> rows;
    IWorkspace* workspace = Host.Workspaces.ActiveIn(Host.Presentation);
    if (workspace == nullptr)
        return rows;
    for (const OperationChoice& choice : workspace->Operation().Choices)
    {
        UiRow row{ choice.Label, Host.Catalog.ChordText(choice.Action), "" };
        row.Key = choice.Action;
        row.Icon = std::string(IconName(choice.Icon));
        row.Selected = choice.Current;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<UiRow> RailMenus::ProblemEntries() const
{
    std::vector<UiRow> rows;
    for (const Problem* problem : Host.Problems.All())
    {
        UiRow entry{ problem->Message, "", "" };
        entry.Key = std::to_string(rows.size());
        rows.push_back(std::move(entry));
    }
    return rows;
}

std::vector<UiRow> RailMenus::ChoiceEntries() const
{
    std::vector<UiRow> rows;
    for (std::size_t i = 0; i < ChoicesListed.size(); ++i)
    {
        const std::string& choice = ChoicesListed[i];
        if (!TextFilterMatch(choice, ChoicesFilter))
            continue;
        UiRow entry{ choice.empty() ? std::string("None") : choice, "",
                     i < ChoicesNotes.size() ? ChoicesNotes[i] : std::string{} };
        entry.Key = choice;
        rows.push_back(std::move(entry));
    }
    return rows;
}

void RailMenus::Publish()
{
    std::string anchor;
    std::vector<UiRow> entries;
    switch (Shown)
    {
    case Popover::Hatch:    anchor = "hatch";                entries = HatchEntries();             break;
    case Popover::Tools:    anchor = "station";              entries = ToolEntries();              break;
    case Popover::Category: anchor = "cat_" + Category;      entries = CategoryEntries(Category);  break;
    case Popover::Problems: anchor = "problems";             entries = ProblemEntries();           break;
    case Popover::Choices:
        anchor = std::format("{}_row_{}", SheetName(ChoicesAt.Sheet), ChoicesAt.Row);
        entries = ChoiceEntries();
        break;
    case Popover::Color:
    case Popover::None:     break;
    }
    UiService& ui = Host.Ui;
    (void)ui.SetValue(Screen, PopoverName, UiValue(Name()));
    (void)ui.SetValue(Screen, Filters,
                      UiValue(Shown == Popover::Choices && ChoicesListed.size() > kChoicesUnfiltered));
    Place(anchor);
    (void)ui.SetRows(Screen, Entries, entries);
}

void RailMenus::Place(std::string_view anchor)
{
    UiService& ui = Host.Ui;
    double x = 0.0;
    bool rises = false;
    std::string top = "auto";
    std::string tallest = "none";
    const float windowHeight = static_cast<float>(Host.Window.GetExtent().Height);
    if (!anchor.empty())
        if (const std::optional<UiElementBox> box = ui.MeasureElement(Screen, anchor))
        {
            x = std::floor(box->X);
            // From an instrument in the lower half, a popover rises instead of
            // hanging below it; either way it ends inside the window.
            rises = OpenTowardRoom(Rect2d(box->X, box->Y, box->Width, box->Height),
                                   { static_cast<float>(Host.Window.GetExtent().Width), windowHeight })
                        .Up;
            const float hangsFrom = std::floor(box->Y + box->Height) + kPopoverGap;
            if (!rises)
                top = PixelLength(hangsFrom);
            tallest = PixelLength(std::floor(rises ? box->Y - kPopoverMargin : windowHeight - hangsFrom - kPopoverMargin));
        }
    (void)ui.SetValue(Screen, Rises, UiValue(rises));
    (void)ui.SetValue(Screen, Top, UiValue(top));
    (void)ui.SetValue(Screen, Tallest, UiValue(tallest));
    // Kept inside the window: an instrument at the right edge opens leftward.
    if (const std::optional<UiElementBox> popover = ui.MeasureElement(Screen, "popover"))
    {
        const double room = static_cast<double>(Host.Window.GetExtent().Width) - popover->Width - kPopoverMargin;
        x = std::max(0.0, std::min(x, room));
    }
    (void)ui.SetValue(Screen, X, UiValue(x));
}
