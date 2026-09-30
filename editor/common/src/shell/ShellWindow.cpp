#include "shell/ShellWindow.h"

#include "actions/ActionCatalog.h"
#include "notices/NoticeQueue.h"
#include "problems/ProblemList.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

#include <platform/SdlWindow.h>
#include <platform/WindowFrameHit.h>
#include <ui/UiService.h>

#include <algorithm>
#include <cmath>

namespace
{
const UiModelPropertyId kWorkspaceLabel = UiPropertyIdAt(0);
const UiModelPropertyId kDocumentLabel = UiPropertyIdAt(1);
const UiModelPropertyId kDocumentChanged = UiPropertyIdAt(2);
const UiModelPropertyId kProblemCount = UiPropertyIdAt(3);
const UiModelPropertyId kProblemErrors = UiPropertyIdAt(4);
const UiModelPropertyId kTransportState = UiPropertyIdAt(5);
const UiModelPropertyId kTransportText = UiPropertyIdAt(6);
const UiModelPropertyId kCookEnabled = UiPropertyIdAt(7);
const UiModelPropertyId kPlayEnabled = UiPropertyIdAt(8);
const UiModelPropertyId kStopEnabled = UiPropertyIdAt(9);
const UiModelPropertyId kPopover = UiPropertyIdAt(10);
const UiModelPropertyId kPopoverX = UiPropertyIdAt(11);

const UiModelRowsId kNotices = UiRowsIdAt(0);
const UiModelRowsId kEntries = UiRowsIdAt(1);

const UiActionId kPopoverAction = UiActionIdAt(0);
const UiActionId kChooseAction = UiActionIdAt(1);
const UiActionId kInvokeAction = UiActionIdAt(2);
const UiActionId kWindowAction = UiActionIdAt(3);

// The rail's height in dp, when the document has not laid out to say.
constexpr float kRailHeightDp = 34.0f;

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://shell.rml";
    desc.ModelName = "shell";
    desc.Properties = {
        UiModelProperty{ "workspace_label", UiValue(std::string{}) },
        UiModelProperty{ "document_label", UiValue(std::string{}) },
        UiModelProperty{ "document_changed", UiValue(false) },
        UiModelProperty{ "problem_count", UiValue(std::int64_t{ 0 }) },
        UiModelProperty{ "problem_errors", UiValue(false) },
        UiModelProperty{ "transport_state", UiValue(std::string("none")) },
        UiModelProperty{ "transport_text", UiValue(std::string{}) },
        UiModelProperty{ "cook_enabled", UiValue(false) },
        UiModelProperty{ "play_enabled", UiValue(false) },
        UiModelProperty{ "stop_enabled", UiValue(false) },
        UiModelProperty{ "popover", UiValue(std::string{}) },
        UiModelProperty{ "popover_x", UiValue(0.0) },
    };
    desc.RowLists = { "notices", "entries" };
    desc.Actions = { "shell_popover", "shell_choose", "shell_invoke", "shell_window" };
    return desc;
}

const char* TransportName(TransportState state)
{
    switch (state)
    {
    case TransportState::Idle:    return "idle";
    case TransportState::Cooking: return "cooking";
    case TransportState::Playing: return "playing";
    case TransportState::Failed:  return "failed";
    case TransportState::None:
    default:                      return "none";
    }
}

const char* NoticeToneName(NoticeTone tone)
{
    switch (tone)
    {
    case NoticeTone::Warning: return "warning";
    case NoticeTone::Failed:  return "failed";
    case NoticeTone::Done:    return "done";
    case NoticeTone::Info:
    default:                  return "info";
    }
}

std::string ArgumentText(const UiAction& action)
{
    return action.Arguments.empty() ? std::string{} : std::string(action.Arguments[0].AsString());
}

UiRow Heading(std::string label)
{
    UiRow row{ std::move(label), "", "" };
    row.Control = UiRowControl::Section;
    return row;
}
} // namespace

ShellWindow::ShellWindow(ShellWindowServices services)
    : Services(std::move(services))
{
    const WindowExtent extent = Services.Window.GetExtent();
    SurfaceId = Services.Ui.CreateSurface("shell", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(SurfaceId, Services.Window.GetId());
    Screen = Services.Ui.OpenScreen(SurfaceId, Describe());

    Transients.SetClosedObserver([this](TransientSurfaces::Id id) {
        if (id != PopoverTransient)
            return;
        PopoverTransient = 0;
        Showing = Popover::None;
        PopoverLayer.Reset();
    });
    PopoverActions.Claim("edit.escape", ActionHandler{ .Invoke = [this] { (void)Transients.Escape(); } });

    Palette = std::make_unique<CommandPalette>(Services.Ui, SurfaceId, Services.Catalog, Services.Actions,
                                               Services.Workspaces);
    (void)Services.Catalog.Define({ .Id = "shell.palette", .Label = "Command Palette", .Category = "View",
                                    .DefaultChords = { KeyChord{ SDLK_K, { .Ctrl = true } } } });
    WindowActions.Claim("shell.palette", ActionHandler{ .Invoke = [this] {
        OpenPopover(Popover::None);
        Palette->Open();
    } });
    WindowLayer = Services.Actions.Push(WindowActions, ActionRank::Window);
}

ShellWindow::~ShellWindow()
{
    WindowLayer.Reset();
    PopoverLayer.Reset();
    Palette.reset();
    if (Screen.IsValid())
        Services.Ui.CloseScreen(Screen);
    if (SurfaceId.IsValid())
        Services.Ui.DestroySurface(SurfaceId);
}

void ShellWindow::Update(double now, float scale)
{
    if (!Screen.IsValid())
        return;
    const WindowExtent extent = Services.Window.GetExtent();
    Services.Ui.SetSurfaceSize(SurfaceId, RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceScale(SurfaceId, scale);

    HandleActions();
    Palette->Update();
    Publish(now);
    PublishCaption();
}

float ShellWindow::RailHeight() const
{
    if (Screen.IsValid())
        if (const std::optional<UiElementBox> rail = Services.Ui.MeasureElement(Screen, "rail"))
            return rail->Height;
    return kRailHeightDp * Services.Ui.GetSurfaceScale(SurfaceId);
}

void ShellWindow::HandleActions()
{
    for (const UiAction& action : Services.Ui.DrainActions(Screen))
    {
        const std::string argument = ArgumentText(action);
        if (action.Id == kPopoverAction)
        {
            const Popover requested = argument == "hatch"      ? Popover::Hatch
                                    : argument == "workspaces" ? Popover::Workspaces
                                    : argument == "document"   ? Popover::Document
                                                               : Popover::None;
            // Pressing the instrument whose popover is open closes it.
            OpenPopover(requested == Showing ? Popover::None : requested);
        }
        else if (action.Id == kChooseAction)
        {
            Choose(argument);
        }
        else if (action.Id == kInvokeAction)
        {
            (void)Services.Actions.Invoke(argument);
        }
        else if (action.Id == kWindowAction)
        {
            if (argument == "minimize")
                (void)Services.Window.Minimize();
            else if (argument == "maximize")
                (void)(Services.Window.IsMaximized() ? Services.Window.Restore() : Services.Window.Maximize());
            else if (argument == "close" && Services.CloseWindow)
                Services.CloseWindow();
        }
    }
}

void ShellWindow::ShowPopover(std::string_view name)
{
    OpenPopover(name == "hatch" ? Popover::Hatch
                : name == "workspaces" ? Popover::Workspaces
                : name == "document" ? Popover::Document
                                     : Popover::None);
}

void ShellWindow::OpenPopover(Popover popover)
{
    if (PopoverTransient != 0)
        Transients.Close(PopoverTransient);
    if (popover == Popover::None)
        return;
    Showing = popover;
    PopoverTransient = Transients.Open({ .Owner = "shell", .Dismiss = TransientDismiss::Light });
    PopoverLayer = Services.Actions.Push(PopoverActions, ActionRank::Surface);
}

void ShellWindow::Choose(const std::string& key)
{
    OpenPopover(Popover::None);
    const std::size_t colon = key.find(':');
    const std::string verb = colon == std::string::npos ? std::string{} : key.substr(0, colon);
    const std::string kind = colon == std::string::npos ? std::string{} : key.substr(colon + 1);
    if (verb == "open")
    {
        const WorkspaceAction action = Services.Workspaces.Find(kind) != nullptr ? WorkspaceAction::Activate
                                                                                : WorkspaceAction::Open;
        Services.Workspaces.Request({ action, kind });
    }
    else if (verb == "close")
        Services.Workspaces.Request({ WorkspaceAction::Close, kind });
    else if (verb == "detach")
        Services.Workspaces.Request({ WorkspaceAction::Detach, kind });
    else if (verb == "attach")
        Services.Workspaces.Request({ WorkspaceAction::Attach, kind });
    else
        (void)Services.Actions.Invoke(key);
}

void ShellWindow::Publish(double now)
{
    UiService& ui = Services.Ui;
    const WorkspaceKind* kind = Services.Workspaces.ActiveKindIn(Services.Presentation);
    const IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    (void)ui.SetValue(Screen, kWorkspaceLabel, UiValue(kind != nullptr ? kind->DisplayName : std::string("KYUSU")));

    const WorkspaceDocumentFacet document = workspace != nullptr ? workspace->Document() : WorkspaceDocumentFacet{};
    (void)ui.SetValue(Screen, kDocumentLabel, UiValue(document.Label));
    std::string title = Services.Title;
    if (kind != nullptr)
        title += " - " + kind->DisplayName;
    if (!document.Label.empty())
        title += " - " + document.Label + (document.Changed ? " *" : "");
    if (title != ShownTitle)
    {
        Services.Window.SetTitle(title);
        ShownTitle = std::move(title);
    }
    (void)ui.SetValue(Screen, kDocumentChanged, UiValue(document.Changed));

    (void)ui.SetValue(Screen, kProblemCount,
                      UiValue(static_cast<std::int64_t>(Services.Problems.Count(ProblemSeverity::Warning))));
    (void)ui.SetValue(Screen, kProblemErrors, UiValue(Services.Problems.Count(ProblemSeverity::Error) > 0));

    const WorkspaceTransportFacet transport = workspace != nullptr ? workspace->Transport() : WorkspaceTransportFacet{};
    (void)ui.SetValue(Screen, kTransportState, UiValue(std::string(TransportName(transport.State))));
    (void)ui.SetValue(Screen, kTransportText, UiValue(transport.Readout));
    (void)ui.SetValue(Screen, kCookEnabled, UiValue(Services.Actions.Query("transport.cook").Enabled));
    (void)ui.SetValue(Screen, kPlayEnabled, UiValue(Services.Actions.Query("transport.play").Enabled));
    (void)ui.SetValue(Screen, kStopEnabled, UiValue(Services.Actions.Query("transport.stop").Enabled));

    std::vector<UiRow> notices;
    for (const Notice& notice : Services.Notices.Showing(now))
        notices.push_back(UiRow{ notice.Text, "", NoticeToneName(notice.Tone) });
    (void)ui.SetRows(Screen, kNotices, notices);

    PublishEntries();
}

void ShellWindow::PublishEntries()
{
    UiService& ui = Services.Ui;
    std::string name;
    std::string anchor;
    std::vector<UiRow> entries;
    switch (Showing)
    {
    case Popover::Hatch:      name = "hatch";      anchor = "hatch";          entries = HatchEntries();     break;
    case Popover::Workspaces: name = "workspaces"; anchor = "workspace_dial"; entries = WorkspaceEntries(); break;
    case Popover::Document:   name = "document";   anchor = "document_dial";  entries = DocumentEntries();  break;
    case Popover::None:       break;
    }
    (void)ui.SetValue(Screen, kPopover, UiValue(name));
    double x = 0.0;
    if (!anchor.empty())
        if (const std::optional<UiElementBox> box = ui.MeasureElement(Screen, anchor))
            x = std::floor(box->X);
    (void)ui.SetValue(Screen, kPopoverX, UiValue(x));
    (void)ui.SetRows(Screen, kEntries, entries);
}

std::vector<UiRow> ShellWindow::HatchEntries() const
{
    // Everything this window can do right now, by category: the menu, with no
    // menu bar. Categories keep the order their first action was defined in,
    // which puts the application's own File and Edit first.
    std::vector<std::string> categories;
    const std::vector<const ActionDefinition*> actions = Services.Catalog.All();
    for (const ActionDefinition* action : actions)
        if (std::find(categories.begin(), categories.end(), action->Category) == categories.end())
            categories.push_back(action->Category);

    std::vector<UiRow> rows;
    for (const std::string& category : categories)
    {
        bool headed = false;
        for (const ActionDefinition* action : actions)
        {
            if (action->Category != category)
                continue;
            const ActionContextStack::State state = Services.Actions.Query(action->Id);
            if (!state.Claimed)
                continue;
            if (!headed)
            {
                rows.push_back(Heading(category));
                headed = true;
            }
            UiRow row{ action->Label, Services.Catalog.ChordText(action->Id), "" };
            row.Key = action->Id;
            row.Disabled = !state.Enabled;
            row.Selected = state.Checked;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::vector<UiRow> ShellWindow::WorkspaceEntries() const
{
    WorkspaceHost& host = Services.Workspaces;
    const WorkspaceKind* active = host.ActiveKindIn(Services.Presentation);
    std::vector<UiRow> rows;
    for (const WorkspaceKind& kind : host.Kinds())
    {
        if (!host.IsOffered(kind.Id))
            continue;
        UiRow row{ kind.DisplayName, host.Find(kind.Id) != nullptr ? "open" : "", "" };
        row.Key = "open:" + kind.Id;
        row.Selected = active != nullptr && active->Id == kind.Id;
        rows.push_back(std::move(row));
    }
    if (active == nullptr)
        return rows;

    rows.push_back(Heading(active->DisplayName));
    const bool detached = Services.Presentation != host.MainWindow();
    UiRow move{ detached ? "Move to Main Window" : "Move to New Window", "", "" };
    move.Key = (detached ? "attach:" : "detach:") + active->Id;
    rows.push_back(std::move(move));
    UiRow close{ "Close " + active->DisplayName, "", "" };
    close.Key = "close:" + active->Id;
    rows.push_back(std::move(close));
    return rows;
}

std::vector<UiRow> ShellWindow::DocumentEntries() const
{
    std::vector<UiRow> rows;
    for (const ActionDefinition* action : Services.Catalog.All())
    {
        if (action->Category != "File")
            continue;
        const ActionContextStack::State state = Services.Actions.Query(action->Id);
        if (!state.Claimed)
            continue;
        UiRow row{ action->Label, Services.Catalog.ChordText(action->Id), "" };
        row.Key = action->Id;
        row.Disabled = !state.Enabled;
        rows.push_back(std::move(row));
    }
    return rows;
}

void ShellWindow::PublishCaption()
{
    // The window is dragged from the rail wherever nothing is mounted: the span
    // between its two clusters. Not while a popover is open, whose catch covers
    // the rail too.
    WindowFrameRegions regions;
    regions.ResizeBorder = static_cast<std::int32_t>(std::lround(4.0f * Services.Ui.GetSurfaceScale(SurfaceId)));
    const std::optional<UiElementBox> rail = Services.Ui.MeasureElement(Screen, "rail");
    const std::optional<UiElementBox> left = Services.Ui.MeasureElement(Screen, "rail_left");
    const std::optional<UiElementBox> right = Services.Ui.MeasureElement(Screen, "rail_right");
    if (rail.has_value() && left.has_value() && right.has_value())
    {
        const float from = left->X + left->Width;
        const float to = right->X;
        if (to > from)
        {
            regions.Caption[0] = WindowRect{ static_cast<std::int32_t>(from), 0,
                                             static_cast<std::int32_t>(to - from),
                                             static_cast<std::int32_t>(rail->Y + rail->Height) };
            regions.CaptionCount = 1;
        }
    }
    regions.CaptionEnabled = Showing == Popover::None;
    Services.Window.SetFrameRegions(regions);
}
