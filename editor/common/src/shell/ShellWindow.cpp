#include "shell/ShellWindow.h"

#include "shell/AssetTabs.h"

#include "actions/ActionCatalog.h"
#include "input/SdlEventTranslation.h"
#include "notices/NoticeQueue.h"
#include "icons/IconNames.h"
#include "problems/ProblemList.h"
#include "properties/PropertyEditSession.h"
#include "text/TextFilterMatch.h"
#include "ui/UiScaleChoice.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

#include <platform/SdlWindow.h>
#include <platform/WindowFrameHit.h>
#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
const UiModelPropertyId kProblemCount = UiPropertyIdAt(0);
const UiModelPropertyId kProblemErrors = UiPropertyIdAt(1);
const UiModelPropertyId kTransportState = UiPropertyIdAt(2);
const UiModelPropertyId kTransportText = UiPropertyIdAt(3);
const UiModelPropertyId kCookEnabled = UiPropertyIdAt(4);
const UiModelPropertyId kPlayEnabled = UiPropertyIdAt(5);
const UiModelPropertyId kStopEnabled = UiPropertyIdAt(6);
const UiModelPropertyId kPopover = UiPropertyIdAt(7);
const UiModelPropertyId kPopoverX = UiPropertyIdAt(8);
const UiModelPropertyId kPopoverRises = UiPropertyIdAt(9);
const UiModelPropertyId kStationName = UiPropertyIdAt(10);
const UiModelPropertyId kStationVariant = UiPropertyIdAt(11);
const UiModelPropertyId kStationIcon = UiPropertyIdAt(12);
const UiModelPropertyId kStationHint = UiPropertyIdAt(13);
const UiModelPropertyId kDraft = UiPropertyIdAt(14);
const UiModelPropertyId kCardOpen = UiPropertyIdAt(15);
const UiModelPropertyId kElement = UiPropertyIdAt(16);
const UiModelPropertyId kGizmoMode = UiPropertyIdAt(17);
const UiModelPropertyId kGizmoSpace = UiPropertyIdAt(18);
const UiModelPropertyId kGrid = UiPropertyIdAt(19);
const UiModelPropertyId kSnap = UiPropertyIdAt(20);
const UiModelPropertyId kPivot = UiPropertyIdAt(21);
const UiModelPropertyId kProjection = UiPropertyIdAt(22);
const UiModelPropertyId kViewCount = UiPropertyIdAt(23);
const UiModelPropertyId kHoldingRole = UiPropertyIdAt(24);
const UiModelPropertyId kHoldingLabel = UiPropertyIdAt(25);
const UiModelPropertyId kHoldingImage = UiPropertyIdAt(26);
const UiModelPropertyId kMotion = UiPropertyIdAt(27);
const UiModelPropertyId kSelectionTitle = UiPropertyIdAt(28);
const UiModelPropertyId kSelectionStaged = UiPropertyIdAt(29);
const UiModelPropertyId kDrawer = UiPropertyIdAt(30);
const UiModelPropertyId kSelectionSubtitle = UiPropertyIdAt(31);
const UiModelPropertyId kHoldingExplorer = UiPropertyIdAt(32);
const UiModelPropertyId kExplorerId = UiPropertyIdAt(33);
const UiModelPropertyId kExplorerTitle = UiPropertyIdAt(34);
const UiModelPropertyId kExplorerShape = UiPropertyIdAt(35);
const UiModelPropertyId kExplorerRange = UiPropertyIdAt(36);
const UiModelPropertyId kExplorerEmpty = UiPropertyIdAt(37);
const UiModelPropertyId kExplorerEarlier = UiPropertyIdAt(38);
const UiModelPropertyId kExplorerLater = UiPropertyIdAt(39);
const UiModelPropertyId kPopoverTop = UiPropertyIdAt(40);
const UiModelPropertyId kSheet = UiPropertyIdAt(41);
const UiModelPropertyId kExplorerLeft = UiPropertyIdAt(42);
const UiModelPropertyId kExplorerTop = UiPropertyIdAt(43);
const UiModelPropertyId kExplorerWidth = UiPropertyIdAt(44);
const UiModelPropertyId kExplorerHeight = UiPropertyIdAt(45);
const UiModelPropertyId kSelectionLeft = UiPropertyIdAt(46);
const UiModelPropertyId kSelectionTop = UiPropertyIdAt(47);
const UiModelPropertyId kSelectionWidth = UiPropertyIdAt(48);
const UiModelPropertyId kSelectionHeight = UiPropertyIdAt(49);
const UiModelPropertyId kFront = UiPropertyIdAt(50);
const UiModelPropertyId kHierarchyChord = UiPropertyIdAt(51);
const UiModelPropertyId kSelectionShown = UiPropertyIdAt(52);
const UiModelPropertyId kMapAction = UiPropertyIdAt(53);
const UiModelPropertyId kPopoverFilters = UiPropertyIdAt(54);
const UiModelPropertyId kSelectionNamed = UiPropertyIdAt(55);
const UiModelPropertyId kSheetTyping = UiPropertyIdAt(56);

const UiModelRowsId kNotices = UiRowsIdAt(0);
const UiModelRowsId kEntries = UiRowsIdAt(1);
const UiModelRowsId kToolRows = UiRowsIdAt(2);
const UiModelRowsId kStash = UiRowsIdAt(3);
const UiModelRowsId kSelectionRows = UiRowsIdAt(4);
const UiModelRowsId kSelectionVerbs = UiRowsIdAt(5);
const UiModelRowsId kExplorerItems = UiRowsIdAt(6);
const UiModelRowsId kExplorerVerbs = UiRowsIdAt(7);
const UiModelRowsId kCrumbs = UiRowsIdAt(8);
const UiModelRowsId kTabs = UiRowsIdAt(9);

const UiActionId kPopoverAction = UiActionIdAt(0);
const UiActionId kChooseAction = UiActionIdAt(1);
const UiActionId kInvokeAction = UiActionIdAt(2);
const UiActionId kWindowAction = UiActionIdAt(3);
const UiActionId kSheetSetAction = UiActionIdAt(4);
const UiActionId kSheetFlipAction = UiActionIdAt(5);
const UiActionId kSheetHoldAction = UiActionIdAt(6);
const UiActionId kSheetReleaseAction = UiActionIdAt(7);
const UiActionId kSheetActAction = UiActionIdAt(8);
const UiActionId kDrawerAction = UiActionIdAt(9);
const UiActionId kSheetPickAction = UiActionIdAt(10);
const UiActionId kExploreAction = UiActionIdAt(11);
const UiActionId kExplorerFilterAction = UiActionIdAt(12);
const UiActionId kExplorerPageAction = UiActionIdAt(13);
const UiActionId kExplorerPickAction = UiActionIdAt(14);
const UiActionId kExplorerCommitAction = UiActionIdAt(15);
const UiActionId kExplorerCloseAction = UiActionIdAt(16);
const UiActionId kExplorerOpenAction = UiActionIdAt(17);
const UiActionId kExplorerMarkAction = UiActionIdAt(18);
const UiActionId kGrabAction = UiActionIdAt(19);
const UiActionId kDragAction = UiActionIdAt(20);
const UiActionId kDropAction = UiActionIdAt(21);
const UiActionId kUnmoveAction = UiActionIdAt(22);
const UiActionId kRaiseAction = UiActionIdAt(23);
const UiActionId kCrumbAction = UiActionIdAt(24);
const UiActionId kTabAction = UiActionIdAt(25);
const UiActionId kTabDropAction = UiActionIdAt(26);
const UiActionId kSheetFoldAction = UiActionIdAt(27);
const UiActionId kPopoverFilterAction = UiActionIdAt(28);
const UiActionId kTitleHoldAction = UiActionIdAt(29);
const UiActionId kTitleReleaseAction = UiActionIdAt(30);
const UiActionId kScrubPressAction = UiActionIdAt(31);
const UiActionId kSheetReleasePartAction = UiActionIdAt(32);
const UiActionId kSheetKeyAction = UiActionIdAt(33);

// The floating surfaces a header can move, as a document names them.
constexpr std::string_view kExplorerSurface = "explorer";
constexpr std::string_view kSelectionSurface = "selection";

// The floating surfaces' gap from the window and the instruments, the step
// free space is searched on, and the smallest a person can size one, in dp.
constexpr float kSurfaceGapDp = 8.0f;
constexpr float kSpaceStepDp = 16.0f;
constexpr float kSurfaceMinimumWidthDp = 220.0f;
constexpr float kSurfaceMinimumHeightDp = 140.0f;

// Choices beyond this many are worth narrowing by typing.
constexpr std::size_t kChoicesUnfiltered = 6;

// How many items an explorer shows at once, and asks its workspace for.
constexpr std::size_t kExplorerPage = 40;

// The rail's height in dp, when the document has not laid out to say.
constexpr float kRailHeightDp = 34.0f;

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://shell.rml";
    desc.ModelName = "shell";
    desc.Properties = {
        UiModelProperty{ "problem_count", UiValue(std::int64_t{ 0 }) },
        UiModelProperty{ "problem_errors", UiValue(false) },
        UiModelProperty{ "transport_state", UiValue(std::string("none")) },
        UiModelProperty{ "transport_text", UiValue(std::string{}) },
        UiModelProperty{ "cook_enabled", UiValue(false) },
        UiModelProperty{ "play_enabled", UiValue(false) },
        UiModelProperty{ "stop_enabled", UiValue(false) },
        UiModelProperty{ "popover", UiValue(std::string{}) },
        UiModelProperty{ "popover_x", UiValue(0.0) },
        UiModelProperty{ "popover_rises", UiValue(false) },
        UiModelProperty{ "station_name", UiValue(std::string{}) },
        UiModelProperty{ "station_variant", UiValue(std::string{}) },
        UiModelProperty{ "station_icon", UiValue(std::string{}) },
        UiModelProperty{ "station_hint", UiValue(std::string{}) },
        UiModelProperty{ "draft", UiValue(std::string("none")) },
        UiModelProperty{ "card_open", UiValue(false) },
        UiModelProperty{ "element", UiValue(std::string{}) },
        UiModelProperty{ "gizmo_mode", UiValue(std::string{}) },
        UiModelProperty{ "gizmo_space", UiValue(std::string{}) },
        UiModelProperty{ "grid", UiValue(std::string{}) },
        UiModelProperty{ "snap", UiValue(false) },
        UiModelProperty{ "pivot", UiValue(false) },
        UiModelProperty{ "projection", UiValue(std::string{}) },
        UiModelProperty{ "view_count", UiValue(std::int64_t{ 0 }) },
        UiModelProperty{ "holding_role", UiValue(std::string{}) },
        UiModelProperty{ "holding_label", UiValue(std::string{}) },
        UiModelProperty{ "holding_image", UiValue(std::string{}) },
        UiModelProperty{ "motion", UiValue(true) },
        UiModelProperty{ "selection_title", UiValue(std::string{}) },
        UiModelProperty{ "selection_staged", UiValue(false) },
        UiModelProperty{ "drawer", UiValue(false) },
        UiModelProperty{ "selection_subtitle", UiValue(std::string{}) },
        UiModelProperty{ "holding_explorer", UiValue(std::string{}) },
        UiModelProperty{ "explorer_id", UiValue(std::string{}) },
        UiModelProperty{ "explorer_title", UiValue(std::string{}) },
        UiModelProperty{ "explorer_shape", UiValue(std::string{}) },
        UiModelProperty{ "explorer_range", UiValue(std::string{}) },
        UiModelProperty{ "explorer_empty", UiValue(std::string{}) },
        UiModelProperty{ "explorer_earlier", UiValue(false) },
        UiModelProperty{ "explorer_later", UiValue(false) },
        UiModelProperty{ "popover_top", UiValue(std::string("auto")) },
        UiModelProperty{ "sheet", UiValue(false) },
        UiModelProperty{ "explorer_left", UiValue(std::string("0px")) },
        UiModelProperty{ "explorer_top", UiValue(std::string("0px")) },
        UiModelProperty{ "explorer_width", UiValue(std::string("auto")) },
        UiModelProperty{ "explorer_height", UiValue(std::string("auto")) },
        UiModelProperty{ "selection_left", UiValue(std::string("0px")) },
        UiModelProperty{ "selection_top", UiValue(std::string("0px")) },
        UiModelProperty{ "selection_width", UiValue(std::string("auto")) },
        UiModelProperty{ "selection_height", UiValue(std::string("auto")) },
        UiModelProperty{ "front", UiValue(std::string{}) },
        UiModelProperty{ "hierarchy_chord", UiValue(std::string{}) },
        UiModelProperty{ "selection_shown", UiValue(false) },
        UiModelProperty{ "map_action", UiValue(std::string{}) },
        UiModelProperty{ "popover_filters", UiValue(false) },
        UiModelProperty{ "selection_named", UiValue(false) },
        UiModelProperty{ "sheet_typing", UiValue(std::string{}) },
    };
    desc.RowLists = { "notices",        "entries",         "tool_rows",       "stash",
                      "selection_rows", "selection_verbs", "explorer_items", "explorer_verbs",
                      "crumbs", "tabs" };
    desc.Actions = { "shell_popover",   "shell_choose",     "shell_invoke",     "shell_window",       "shell_sheet_set",
                     "shell_sheet_flip", "shell_sheet_hold", "shell_sheet_release", "shell_sheet_act", "shell_drawer",
                     "shell_sheet_pick", "shell_explore", "shell_explorer_filter", "shell_explorer_page",
                     "shell_explorer_pick", "shell_explorer_commit", "shell_explorer_close",
                     "shell_explorer_open", "shell_explorer_mark", "shell_grab", "shell_drag", "shell_drop",
                     "shell_unmove", "shell_raise", "shell_crumb", "shell_tab", "shell_tab_drop",
                     "shell_sheet_fold", "shell_popover_filter", "shell_title_hold", "shell_title_release",
                     "shell_scrub_press", "shell_sheet_release_part", "shell_sheet_key" };
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

const char* DraftName(ToolDraft draft)
{
    switch (draft)
    {
    case ToolDraft::Incomplete: return "incomplete";
    case ToolDraft::Ready:      return "ready";
    case ToolDraft::None:
    default:                    return "none";
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

std::string Pixels(float value)
{
    return std::format("{}px", std::round(value));
}

} // namespace

ShellWindow::ShellWindow(ShellWindowServices services)
    : Services(std::move(services))
{
    const WindowExtent extent = Services.Window.GetExtent();
    SurfaceId = Services.Ui.CreateSurface("shell", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(SurfaceId, Services.Window.GetId());
    Screen = Services.Ui.OpenScreen(SurfaceId, Describe());

    ContentSurfaceId = Services.Ui.CreateSurface("content", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(ContentSurfaceId, Services.Window.GetId());
    Services.Ui.SetSurfaceLayer(ContentSurfaceId, kContentUiLayer);
    Services.Ui.SetSurfaceInputPolicy(ContentSurfaceId, UiSurfaceInputPolicy::Disabled);

    // Created after the shell's surface, so it draws over it.
    OverlaySurfaceId = Services.Ui.CreateSurface("overlay", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(OverlaySurfaceId, Services.Window.GetId());
    Services.Ui.SetSurfaceInputPolicy(OverlaySurfaceId, UiSurfaceInputPolicy::Disabled);

    Transients.SetClosedObserver([this](TransientSurfaces::Id id) {
        if (id == PopoverTransient)
        {
            PopoverTransient = 0;
            Showing = Popover::None;
        }
        else if (id == ExplorerTransient)
        {
            ExplorerTransient = 0;
            ExplorerShown.clear();
        }
        else if (id == DrawerTransient)
        {
            DrawerTransient = 0;
            SelectionShown = SelectionView::Card;
        }
        if (Transients.OpenIds().empty())
            EscapeLayer.Reset();
    });
    ToolSheet.OfferActions(Services.Catalog, Services.Actions);
    SelectionSheet.OfferActions(Services.Catalog, Services.Actions);
    // Escape belongs to what is open while anything is.
    EscapeActions.Claim("edit.escape", ActionHandler{ .Invoke = [this] { EscapeFront(); } });

    Palette = std::make_unique<CommandPalette>(Services.Ui, SurfaceId, Services.Catalog, Services.Actions,
                                               Services.Workspaces);
    (void)Services.Catalog.Define({ .Id = "shell.palette", .Label = "Command Palette", .Category = "View",
                                    .DefaultChords = { KeyChord{ SDLK_K, { .Ctrl = true } } } });
    WindowActions.Claim("shell.palette", ActionHandler{ .Invoke = [this] {
        OpenPopover(Popover::None);
        Palette->Open();
    } });
    (void)Services.Catalog.Define({ .Id = "selection.details", .Label = "All Properties", .Category = "Edit" });
    WindowActions.Claim("selection.details", ActionHandler{ .Invoke = [this] { ShowDrawer(); },
                                                            .Enabled = [this] { return !ShownSelection.empty(); } });
    WindowLayer = Services.Actions.Push(WindowActions, ActionRank::Window);
    Deck = std::make_unique<TimelineDeck>(Services.Ui, SurfaceId, Services.Workspaces, Services.Actions,
                                          Services.Presentation);
    Prompt = std::make_unique<PromptDialog>(Services.Ui, SurfaceId, Services.Prompts, Services.Actions);
}

ShellWindow::~ShellWindow()
{
    WindowLayer.Reset();
    EscapeLayer.Reset();
    Prompt.reset();
    Deck.reset();
    Palette.reset();
    if (Screen.IsValid())
        Services.Ui.CloseScreen(Screen);
    if (SurfaceId.IsValid())
        Services.Ui.DestroySurface(SurfaceId);
    if (ContentSurfaceId.IsValid())
        Services.Ui.DestroySurface(ContentSurfaceId);
    if (OverlaySurfaceId.IsValid())
        Services.Ui.DestroySurface(OverlaySurfaceId);
}

WorkspaceWindow ShellWindow::Binding() const
{
    return { .Window = &Services.Window,
             .Presentation = Services.Presentation,
             .Content = ContentSurfaceId,
             .Overlay = OverlaySurfaceId,
             .Interactive = SurfaceId,
             .Actions = &Services.Actions };
}

void ShellWindow::Update(double now, const ShellPreferences& preferences)
{
    if (!Screen.IsValid())
        return;
    const WindowExtent extent = Services.Window.GetExtent();
    const float scale = ChooseUiScale(preferences.Scale, Services.Window.GetDisplayScale());
    (void)Services.Ui.SetValue(Screen, kMotion, UiValue(preferences.Motion));
    for (const UiSurfaceId surface : { SurfaceId, ContentSurfaceId, OverlaySurfaceId })
    {
        Services.Ui.SetSurfaceSize(surface, RenderExtent{ extent.Width, extent.Height });
        Services.Ui.SetSurfaceScale(surface, scale);
    }

    if (IWorkspace* active = Services.Workspaces.ActiveIn(Services.Presentation))
    {
        const float rail = RailHeight();
        active->SetContentRect(Rect2d(Vec2d(0.0f, rail), Vec2d(static_cast<float>(extent.Width),
                                                               std::max(0.0f, static_cast<float>(extent.Height) - rail))));
    }
    YieldPointerToContent();
    HandleActions();
    Prompt->Update();
    // A question stands alone: nothing else opened stays open under it.
    if (Prompt->IsShowing())
    {
        OpenPopover(Popover::None);
        Palette->Close();
    }
    Palette->Update();
    if (Prompt->IsShowing())
        Deck->Hide();
    else
        Deck->Update();
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
            // Pressing the instrument whose popover is open closes it.
            if (argument == PopoverName())
                OpenPopover(Popover::None);
            else
                OpenNamedPopover(argument);
        }
        else if (action.Id == kChooseAction)
        {
            Choose(argument);
        }
        else if (action.Id == kInvokeAction)
        {
            (void)Services.Actions.Invoke(argument);
        }
        else if (action.Id == kSheetSetAction || action.Id == kSheetFlipAction || action.Id == kSheetHoldAction
                 || action.Id == kSheetReleaseAction || action.Id == kSheetActAction || action.Id == kSheetPickAction
                 || action.Id == kSheetFoldAction || action.Id == kScrubPressAction
                 || action.Id == kSheetReleasePartAction || action.Id == kSheetKeyAction)
        {
            HandleSheetAction(action);
        }
        else if (action.Id == kExploreAction)
        {
            if (argument == ExplorerShown)
                Transients.Close(ExplorerTransient);
            else
                ShowExplorer(argument);
        }
        else if (action.Id == kPopoverFilterAction)
        {
            ChoicesFilter = argument;
        }
        else if (action.Id == kExplorerFilterAction)
        {
            ExplorerFilter = argument;
            ExplorerFirst = 0;
        }
        else if (action.Id == kExplorerPageAction)
        {
            const std::int64_t step = action.Arguments.empty() ? 0 : action.Arguments[0].AsInt();
            if (step < 0)
                ExplorerFirst -= std::min(ExplorerFirst, kExplorerPage);
            else if (step > 0)
                ExplorerFirst += kExplorerPage;
        }
        else if (action.Id == kExplorerPickAction || action.Id == kExplorerCommitAction
                 || action.Id == kExplorerOpenAction || action.Id == kExplorerMarkAction)
        {
            IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
            if (workspace == nullptr)
                continue;
            // A press on a row's arrow or mark reaches the row too: it did
            // what it was for, so the row does not also pick.
            if (action.Id == kExplorerPickAction && argument == HandledInRow)
                continue;
            HandledInRow.clear();
            const auto flag = [&](std::size_t at) {
                return action.Arguments.size() > at && action.Arguments[at].Kind() == UiValueKind::Bool
                    && action.Arguments[at].AsBool();
            };
            if (action.Id == kExplorerOpenAction || action.Id == kExplorerMarkAction)
            {
                const std::size_t mark = action.Arguments.size() > 1
                    ? static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arguments[1].AsInt()))
                    : 0;
                workspace->ChooseInExplorer(ExplorerShown, argument,
                                            action.Id == kExplorerOpenAction ? ExplorerChoice::Open : ExplorerChoice::Mark,
                                            mark);
                HandledInRow = argument;
                continue;
            }
            const ExplorerChoice choice = action.Id == kExplorerCommitAction ? ExplorerChoice::Commit
                                        : flag(1)                            ? ExplorerChoice::Extend
                                        : flag(2)                            ? ExplorerChoice::Toggle
                                                                             : ExplorerChoice::Pick;
            workspace->ChooseInExplorer(ExplorerShown, argument, choice, 0);
            // Putting an item to use is what an explorer of pictures was open
            // for; an outline stays, since it is where the structure is worked.
            const std::vector<WorkspaceExplorerInfo> explorers = workspace->Explorers();
            const auto shown = std::ranges::find(explorers, ExplorerShown, &WorkspaceExplorerInfo::Id);
            if (choice == ExplorerChoice::Commit && ExplorerTransient != 0 && shown != explorers.end()
                && shown->Shape == ExplorerShape::Pictures)
                Transients.Close(ExplorerTransient);
        }
        else if (action.Id == kExplorerCloseAction)
        {
            if (ExplorerTransient != 0)
                Transients.Close(ExplorerTransient);
        }
        else if (action.Id == kDrawerAction)
        {
            if (SelectionShown == SelectionView::Drawer)
                Transients.Close(DrawerTransient);
            else
                ShowDrawer();
        }
        else if (action.Id == kTitleHoldAction)
        {
            TitleHeld = true;
        }
        else if (action.Id == kTitleReleaseAction)
        {
            TitleHeld = false;
            RenameSelection();
        }
        else if (action.Id == kGrabAction || action.Id == kDragAction || action.Id == kDropAction
                 || action.Id == kUnmoveAction || action.Id == kRaiseAction)
        {
            HandleMoveAction(action);
        }
        else if (action.Id == kTabAction || action.Id == kTabDropAction)
        {
            HandleTabAction(action);
        }
        else if (action.Id == kCrumbAction)
        {
            RevealLocation(action.Arguments.empty() ? -1 : action.Arguments[0].AsInt());
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

void ShellWindow::HandleSheetAction(const UiAction& action)
{
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    if (workspace == nullptr || action.Arguments.size() < 2)
        return;
    const bool tool = action.Arguments[0].AsString() == "tool";
    const std::int64_t row = action.Arguments[1].AsInt();
    if (row < 0)
        return;
    const auto index = static_cast<std::size_t>(row);
    const std::vector<PropertySection> sections = SheetSections(tool ? "tool" : "selection", *workspace);
    PropertySheet& sheet = tool ? ToolSheet : SelectionSheet;

    if (action.Id == kSheetHoldAction)
        sheet.Hold(index);
    else if (action.Id == kSheetReleaseAction)
    {
        const std::vector<UiRow> shown = Services.Ui.GetRows(Screen, tool ? kToolRows : kSelectionRows);
        (void)sheet.Release(sections, index, index < shown.size() ? std::string_view(shown[index].Value) : std::string_view{});
    }
    else if (action.Id == kSheetFlipAction)
        (void)sheet.Flip(sections, index);
    else if (action.Id == kSheetActAction && action.Arguments.size() > 2)
    {
        // A verb that asks for a choice first opens its choices under it.
        const std::string verb(action.Arguments[2].AsString());
        const std::vector<std::string> choices = sheet.ChoicesOf(index, verb);
        const std::string chosen = action.Arguments.size() > 3 ? std::string(action.Arguments[3].AsString()) : std::string{};
        if (!choices.empty() && chosen.empty())
            OpenChoices(tool ? "tool" : "selection", index, verb, choices, sheet.NotesOf(index, verb));
        else
            ActOnSheet(sheet, sections, index, verb, chosen);
    }
    else if (action.Id == kSheetFoldAction)
        sheet.Fold(index);
    else if (action.Id == kScrubPressAction && action.Arguments.size() > 2)
        PressNumber(tool ? "tool" : "selection", index, action.Arguments[2].AsInt());
    else if (action.Id == kSheetReleasePartAction && action.Arguments.size() > 2)
    {
        // Let go of after Enter, Tab or Escape already ended it: nothing more.
        const std::int64_t element = action.Arguments[2].AsInt();
        if (SheetTyping == TypingKey(tool ? "tool" : "selection", index, element))
            FinishTyping(sheet, sections, tool, index, element);
    }
    else if (action.Id == kSheetKeyAction && action.Arguments.size() > 3)
    {
        const std::int64_t element = action.Arguments[2].AsInt();
        const std::optional<UiKey> key = UiKeyFromEvent(action.Arguments[3].AsInt());
        if (key == UiKey::Escape)
        {
            sheet.Drop();
            SheetTyping.clear();
        }
        else if (key == UiKey::Return || key == UiKey::Tab)
        {
            const std::size_t parts = FinishTyping(sheet, sections, tool, index, element);
            // Tab goes on to the next number of the same vector.
            if (key == UiKey::Tab && element >= 0 && static_cast<std::size_t>(element) + 1 < parts)
                SheetTyping = TypingKey(tool ? "tool" : "selection", index, element + 1);
        }
    }
    else if (action.Id == kSheetPickAction)
    {
        std::vector<std::string> candidates = sheet.CandidatesOf(sections, index);
        candidates.insert(candidates.begin(), std::string{});
        OpenChoices(tool ? "tool" : "selection", index, {}, std::move(candidates));
    }
    else if (action.Arguments.size() > 2)
        (void)sheet.SetFromControl(sections, index, action.Arguments[2]);
}

namespace
{
std::optional<double> NumberIn(std::string_view text)
{
    double value = 0.0;
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || text.empty())
        return std::nullopt;
    return value;
}

std::string NumberText(double value)
{
    return std::format("{:.6g}", value);
}
} // namespace

void ShellWindow::PressNumber(std::string sheet, std::size_t row, std::int64_t element)
{
    PropertySheet& presented = sheet == "tool" ? ToolSheet : SelectionSheet;
    const std::vector<UiRow> shown = Services.Ui.GetRows(Screen, sheet == "tool" ? kToolRows : kSelectionRows);
    if (row >= shown.size() || !shown[row].Editable)
        return;
    const std::optional<std::size_t> part =
        element >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(element)) : std::nullopt;
    const std::string text = part.has_value() ? (*part < shown[row].Parts.size() ? shown[row].Parts[*part] : "")
                                              : shown[row].Value;
    const std::optional<double> start = NumberIn(text);
    ScrubSheet = std::move(sheet);
    ScrubRow = row;
    ScrubElement = element;
    ScrubMotion = ScrubGesture(ScrubScaleFor(presented.BoundsOf(row, part)));
    ScrubMixed = !start.has_value();
    ScrubStart = start.value_or(0.0);
    ScrubParts = shown[row].Parts;
    ScrubAmount = 0.0;
    Scrub = ScrubPhase::Pressed;
}

bool ShellWindow::TakeScrubInput(const InputEvent& event)
{
    if (Scrub == ScrubPhase::Resting)
        return false;
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
    {
        const bool began = Scrub == ScrubPhase::Pressed;
        if (!ScrubMotion.Move(move->Delta.X, move->Modifiers.Shift))
            return true;
        if (began)
        {
            IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
            PropertySheet& sheet = ScrubSheet == "tool" ? ToolSheet : SelectionSheet;
            const std::vector<PropertySection> sections =
                workspace != nullptr ? SheetSections(ScrubSheet, *workspace) : std::vector<PropertySection>{};
            IPropertySource* source = sheet.SourceOf(sections, ScrubRow);
            const std::optional<std::size_t> part =
                ScrubElement >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(ScrubElement)) : std::nullopt;
            std::string key = sheet.EditKeyOf(ScrubRow, part);
            // A number its source names no key for is moved by setting its vector whole.
            if (key.empty())
                key = sheet.EditKeyOf(ScrubRow);
            if (source == nullptr || key.empty() || !ScrubSession.Begin(*source, key))
            {
                Scrub = ScrubPhase::Resting;
                return false;
            }
            ScrubWrites = part.has_value() && key == sheet.EditKeyOf(ScrubRow) ? ScrubTarget::WholeVector
                                                                               : ScrubTarget::Number;
            Scrub = ScrubPhase::Dragging;
            SetRelativeMouseMode(Services.Window, true);
        }
        PreviewScrub(move->Modifiers.Ctrl);
        return true;
    }
    if (const auto* up = std::get_if<PointerUpEvent>(&event); up != nullptr && up->Button == MouseButton::Left)
    {
        // Let go where it was pressed: a click, which types into the number instead.
        if (Scrub == ScrubPhase::Pressed)
            SheetTyping = TypingKey(ScrubSheet, ScrubRow, ScrubElement);
        EndScrub(true);
        return true;
    }
    if (const auto* key = std::get_if<KeyDownEvent>(&event); key != nullptr && key->Key == SDLK_ESCAPE)
    {
        EndScrub(false);
        return true;
    }
    if (std::holds_alternative<FocusLostEvent>(event))
        EndScrub(false);
    return false;
}

std::string ShellWindow::ScrubbedText() const
{
    const std::string moved = NumberText(ScrubStart + ScrubAmount);
    if (ScrubWrites == ScrubTarget::Number)
        return moved;
    std::string joined;
    for (std::size_t i = 0; i < ScrubParts.size(); ++i)
        joined += (i == 0 ? "" : ", ") + (static_cast<std::int64_t>(i) == ScrubElement ? moved : ScrubParts[i]);
    return joined;
}

void ShellWindow::PreviewScrub(bool coarse)
{
    ScrubAmount = ScrubMotion.Amount(ScrubMixed ? 0.0 : ScrubStart, coarse);
    if (ScrubMixed)
        (void)ScrubSession.PreviewBy(ScrubAmount);
    else
        (void)ScrubSession.Preview(ScrubbedText());
}

void ShellWindow::EndScrub(bool commit)
{
    if (Scrub == ScrubPhase::Dragging)
    {
        if (!commit)
            ScrubSession.Cancel();
        else if (ScrubMixed)
            (void)ScrubSession.CommitBy(ScrubAmount);
        else
            (void)ScrubSession.Commit(ScrubbedText());
        SetRelativeMouseMode(Services.Window, false);
    }
    Scrub = ScrubPhase::Resting;
}

std::string ShellWindow::TypingKey(std::string_view sheet, std::size_t row, std::int64_t element)
{
    return std::format("{}:{}:{}", sheet, row, element);
}

std::size_t ShellWindow::FinishTyping(PropertySheet& sheet, std::span<const PropertySection> sections, bool tool,
                                      std::size_t index, std::int64_t element)
{
    SheetTyping.clear();
    const std::vector<UiRow> shown = Services.Ui.GetRows(Screen, tool ? kToolRows : kSelectionRows);
    if (index >= shown.size())
    {
        sheet.Drop();
        return 0;
    }
    const UiRow& row = shown[index];
    if (element < 0)
        (void)sheet.Release(sections, index, row.Value);
    else if (static_cast<std::size_t>(element) < row.Parts.size())
        (void)sheet.ReleaseElement(sections, index, static_cast<std::size_t>(element),
                                   row.Parts[static_cast<std::size_t>(element)]);
    return row.Parts.size();
}

void ShellWindow::ActOnSheet(PropertySheet& sheet, std::span<const PropertySection> sections, std::size_t index,
                             std::string_view verb, std::string_view choice)
{
    const bool removes = sheet.Removes(index, verb);
    const std::string label = sheet.LabelOf(index);
    if (sheet.Act(sections, index, verb, choice) != PropertyEditResult::Applied || !removes)
        return;
    const std::string undo = Services.Catalog.ChordText("edit.undo");
    Services.Notices.Post(undo.empty() ? std::format("Removed {}", label)
                                       : std::format("Removed {} \xC2\xB7 {} puts it back", label, undo),
                          NoticeTone::Info, NoticeClockNow());
}

void ShellWindow::YieldPointerToContent()
{
    // A number pressed in a sheet holds the pointer too: what it does next is
    // the shell's to read, not the document's.
    const IWorkspace* active = Services.Workspaces.ActiveIn(Services.Presentation);
    const UiSurfaceInputPolicy policy = (active != nullptr && active->HoldsPointer()) || Scrubbing()
                                            ? UiSurfaceInputPolicy::Disabled
                                            : UiSurfaceInputPolicy::Full;
    if (policy == InputPolicy)
        return;
    Services.Ui.SetSurfaceInputPolicy(SurfaceId, policy);
    InputPolicy = policy;
}

void ShellWindow::ShowPopover(std::string_view name)
{
    OpenNamedPopover(name);
}

void ShellWindow::OpenNamedPopover(std::string_view name)
{
    constexpr std::string_view kCategory = "category:";
    if (name.starts_with(kCategory))
        OpenPopover(Popover::Category, std::string(name.substr(kCategory.size())));
    else
        OpenPopover(name == "hatch"        ? Popover::Hatch
                    : name == "workspaces" ? Popover::Workspaces
                    : name == "tools"      ? Popover::Tools
                    : name == "problems"   ? Popover::Problems
                                           : Popover::None);
}

std::string ShellWindow::PopoverName() const
{
    switch (Showing)
    {
    case Popover::Hatch:      return "hatch";
    case Popover::Workspaces: return "workspaces";
    case Popover::Tools:      return "tools";
    case Popover::Category:   return "category:" + PopoverCategory;
    case Popover::Choices:    return "choices";
    case Popover::Problems:   return "problems";
    case Popover::None:
    default:                  return {};
    }
}

void ShellWindow::OpenChoices(std::string sheet, std::size_t row, std::string verb, std::vector<std::string> choices,
                              std::vector<std::string> notes)
{
    OpenPopover(Popover::Choices);
    ChoicesSheet = std::move(sheet);
    ChoicesRow = row;
    ChoicesVerb = std::move(verb);
    ChoicesListed = std::move(choices);
    ChoicesNotes = std::move(notes);
    ChoicesFilter.clear();
}

void ShellWindow::OpenPopover(Popover popover, std::string category)
{
    if (PopoverTransient != 0)
        Transients.Close(PopoverTransient);
    if (popover == Popover::None)
        return;
    Showing = popover;
    PopoverCategory = popover == Popover::Category ? std::move(category) : std::string{};
    PopoverTransient = Transients.Open({ .Owner = "shell", .Dismiss = TransientDismiss::Light, .Priority = 1 });
    TakeEscape();
}

void ShellWindow::RevealProblem(std::string_view index)
{
    const std::vector<const Problem*> problems = Services.Problems.All();
    std::size_t at = 0;
    if (std::from_chars(index.data(), index.data() + index.size(), at).ec != std::errc{} || at >= problems.size())
        return;
    const Problem& problem = *problems[at];
    // The workspace whose document it is shows where; the one in this window
    // is asked first.
    IWorkspace* active = Services.Workspaces.ActiveIn(Services.Presentation);
    std::vector<IWorkspace*> asked;
    if (active != nullptr)
        asked.push_back(active);
    for (const WorkspaceKind& kind : Services.Workspaces.Kinds())
        if (IWorkspace* open = Services.Workspaces.Find(kind.Id); open != nullptr && open != active)
            asked.push_back(open);
    for (IWorkspace* workspace : asked)
        if (!problem.Document.has_value() || workspace->OwnsDocument(*problem.Document))
            if (workspace->RevealProblem(problem))
                return;
}

void ShellWindow::EscapeFront()
{
    // A popover is the closest thing; after it, whichever surface is in front.
    const TransientSurfaces::Id front = FrontSurface == kExplorerSurface ? ExplorerTransient
                                      : FrontSurface == kSelectionSurface ? DrawerTransient
                                                                          : 0;
    if (PopoverTransient == 0 && front != 0 && Transients.IsOpen(front))
        Transients.Close(front);
    else
        (void)Transients.Escape();
}

void ShellWindow::TakeEscape()
{
    if (!EscapeLayer.IsActive())
        EscapeLayer = Services.Actions.Push(EscapeActions, ActionRank::Surface);
}

void ShellWindow::ShowExplorer(std::string explorer)
{
    if (ExplorerTransient != 0)
        Transients.Close(ExplorerTransient);
    if (explorer.empty())
        return;
    ExplorerShown = std::move(explorer);
    ExplorerFirst = 0;
    FrontSurface = kExplorerSurface;
    ExplorerTransient = Transients.Open({ .Owner = "shell", .Dismiss = TransientDismiss::Explicit });
    TakeEscape();
}

void ShellWindow::ClaimExplorers(const IWorkspace* workspace)
{
    std::vector<WorkspaceExplorerInfo> explorers = workspace != nullptr ? workspace->Explorers()
                                                                        : std::vector<WorkspaceExplorerInfo>{};
    std::vector<std::string> actions;
    for (const WorkspaceExplorerInfo& explorer : explorers)
        actions.push_back(explorer.Action);
    if (actions == ClaimedExplorers)
        return;
    for (const std::string& action : ClaimedExplorers)
        WindowActions.Release(action);
    for (const WorkspaceExplorerInfo& explorer : explorers)
        WindowActions.Claim(explorer.Action, ActionHandler{ .Invoke = [this, id = explorer.Id] {
                                if (id == ExplorerShown)
                                    Transients.Close(ExplorerTransient);
                                else
                                    ShowExplorer(id);
                            } });
    ClaimedExplorers = std::move(actions);
    // What was open belongs to the workspace that was showing.
    if (!ExplorerShown.empty()
        && std::ranges::none_of(explorers, [&](const WorkspaceExplorerInfo& e) { return e.Id == ExplorerShown; }))
        Transients.Close(ExplorerTransient);
}

void ShellWindow::PublishExplorer(IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    ClaimExplorers(workspace);
    const std::vector<WorkspaceExplorerInfo> explorers = workspace != nullptr ? workspace->Explorers()
                                                                              : std::vector<WorkspaceExplorerInfo>{};
    const auto shown = std::ranges::find(explorers, ExplorerShown, &WorkspaceExplorerInfo::Id);
    if (shown == explorers.end() || workspace == nullptr)
    {
        ExplorerKey.clear();
        (void)ui.SetValue(Screen, kExplorerId, UiValue(std::string{}));
        return;
    }
    ExplorerKey = std::format("{}.{}", kExplorerSurface,
                              shown->Shape == ExplorerShape::Outline ? "outline" : "pictures");
    ExplorerPage page = workspace->Explore({ .Explorer = ExplorerShown,
                                             .Filter = ExplorerFilter,
                                             .First = ExplorerFirst,
                                             .Count = kExplorerPage });
    // A narrower filter can leave the page past the end.
    if (ExplorerFirst > 0 && ExplorerFirst >= page.Total)
    {
        ExplorerFirst = 0;
        page = workspace->Explore({ .Explorer = ExplorerShown, .Filter = ExplorerFilter, .First = 0,
                                    .Count = kExplorerPage });
    }
    (void)ui.SetValue(Screen, kExplorerId, UiValue(ExplorerShown));
    (void)ui.SetValue(Screen, kExplorerTitle, UiValue(shown->Title));
    (void)ui.SetValue(Screen, kExplorerShape,
                      UiValue(std::string(shown->Shape == ExplorerShape::Outline ? "outline" : "pictures")));
    (void)ui.SetValue(Screen, kExplorerRange,
                      UiValue(page.Total == 0 ? std::string{}
                                              : std::format("{}-{} of {}", ExplorerFirst + 1,
                                                            ExplorerFirst + page.Items.size(), page.Total)));
    (void)ui.SetValue(Screen, kExplorerEmpty, UiValue(page.Items.empty() ? page.Empty : std::string{}));
    (void)ui.SetValue(Screen, kExplorerEarlier, UiValue(ExplorerFirst > 0));
    (void)ui.SetValue(Screen, kExplorerLater, UiValue(ExplorerFirst + page.Items.size() < page.Total));

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
    (void)ui.SetRows(Screen, kExplorerItems, items);

    // What cannot be done now is not offered, rather than shown greyed.
    std::vector<UiRow> verbs;
    for (const std::string& id : page.Actions)
        if (const ActionDefinition* definition = Services.Catalog.Find(id);
            definition != nullptr && Services.Actions.Query(id).Enabled)
        {
            UiRow verb{ definition->Label, "", "" };
            verb.Key = id;
            verbs.push_back(std::move(verb));
        }
    (void)ui.SetRows(Screen, kExplorerVerbs, verbs);
}

std::optional<Rect2d> ShellWindow::MeasureBorder(std::string_view elementId) const
{
    const std::optional<UiElementBox> box = Services.Ui.MeasureElement(Screen, elementId, UiBoxArea::Border);
    if (!box.has_value() || box->Width <= 0.0f || box->Height <= 0.0f)
        return std::nullopt;
    return Rect2d(box->X, box->Y, box->Width, box->Height);
}

Rect2d ShellWindow::FloatingBounds() const
{
    const WindowExtent extent = Services.Window.GetExtent();
    const float rail = RailHeight();
    return Rect2d(0.0f, rail, static_cast<float>(extent.Width), std::max(0.0f, static_cast<float>(extent.Height) - rail));
}

void ShellWindow::HandleMoveAction(const UiAction& action)
{
    const std::string surface = ArgumentText(action);
    const bool explorer = surface == kExplorerSurface;
    if (!explorer && surface != kSelectionSurface)
        return;
    if (action.Id == kRaiseAction)
    {
        FrontSurface = surface;
        return;
    }
    const std::string& key = explorer ? ExplorerKey : SelectionKey;
    if (key.empty())
        return;
    const Vec2d pointer = action.Arguments.size() >= 3 ? Vec2d(static_cast<float>(action.Arguments[1].AsFloat()),
                                                               static_cast<float>(action.Arguments[2].AsFloat()))
                                                       : Vec2d();
    if (action.Id == kGrabAction)
    {
        FrontSurface = surface;
        const bool corner = action.Arguments.size() >= 4 && action.Arguments[3].AsString() == "corner";
        if (corner)
            Layout.PressCorner(key, pointer);
        else
            Layout.PressHeader(key, pointer);
    }
    else if (action.Id == kDragAction)
    {
        const float dp = Services.Ui.GetSurfaceScale(SurfaceId);
        Layout.Drag(pointer, FloatingBounds(), Vec2d(kSurfaceMinimumWidthDp * dp, kSurfaceMinimumHeightDp * dp));
    }
    else if (action.Id == kDropAction)
    {
        Layout.Drop();
    }
    else
    {
        Layout.Reset(key);
    }
}

void ShellWindow::PlaceFloatingSurfaces()
{
    UiService& ui = Services.Ui;
    const float dp = ui.GetSurfaceScale(SurfaceId);
    const float gap = kSurfaceGapDp * dp;
    const Rect2d window = FloatingBounds();
    const Rect2d bounds = Rect2d::FromMinMax(window.Min() + Vec2d(gap, gap), window.Max() - Vec2d(gap, gap));

    const std::optional<Rect2d> station = MeasureBorder(ToolCardUp ? "station_bay" : "station");
    const std::optional<Rect2d> holding = MeasureBorder("holding");
    const std::optional<Rect2d> view = MeasureBorder("cat_View");
    const std::optional<Rect2d> crumbs = MeasureBorder("crumbs");
    std::vector<Rect2d> instruments;
    for (const std::optional<Rect2d>& instrument : { station, MeasureBorder("readout"), holding, view, crumbs })
        if (instrument.has_value())
            instruments.push_back(*instrument);
    if (const std::optional<UiElementBox> deck = Deck->Area())
        instruments.emplace_back(deck->X, deck->Y, deck->Width, deck->Height);
    // The lowest a surface reaches: above the stations along the bottom.
    float floor = bounds.Max().Y;
    for (const std::optional<Rect2d>& instrument : { station, holding })
        if (instrument.has_value())
            floor = std::min(floor, instrument->Min().Y - gap);

    const auto publish = [&](const Rect2d& rect, bool autoHeight, UiModelPropertyId left, UiModelPropertyId top,
                             UiModelPropertyId width, UiModelPropertyId height) {
        (void)ui.SetValue(Screen, left, UiValue(Pixels(rect.Min().X)));
        (void)ui.SetValue(Screen, top, UiValue(Pixels(rect.Min().Y)));
        (void)ui.SetValue(Screen, width, UiValue(Pixels(rect.Width())));
        (void)ui.SetValue(Screen, height, UiValue(autoHeight ? std::string("auto") : Pixels(rect.Height())));
    };
    const auto follow = [&](std::string& shown, const std::string& key) {
        if (shown != key && !shown.empty())
            Layout.Hide(shown);
        shown = key;
    };

    // The selection keeps its own place; what is summoned finds room around it.
    follow(PlacedSelectionKey, SelectionKey);
    std::optional<Rect2d> selection;
    if (!SelectionKey.empty())
    {
        Rect2d home;
        bool autoHeight = false;
        if (SelectionKey.ends_with(".sheet"))
        {
            const float width = std::min(760.0f * dp, bounds.Width());
            const float top = bounds.Min().Y + 36.0f * dp;
            home = Rect2d(bounds.Center().X - width / 2.0f, top, width, std::max(0.0f, floor - top));
        }
        else if (SelectionKey.ends_with(".drawer"))
        {
            const float top = view.has_value() ? view->Max().Y + gap : bounds.Min().Y;
            home = Rect2d(bounds.Max().X - 360.0f * dp, top, 360.0f * dp, std::max(0.0f, floor - top));
        }
        else
        {
            const std::optional<Rect2d> card = MeasureBorder("selection_card");
            const float height = card.has_value() ? card->Height() : 200.0f * dp;
            home = Rect2d(bounds.Max().X - 300.0f * dp, floor - height, 300.0f * dp, height);
            autoHeight = !Layout.Sized(SelectionKey);
        }
        selection = Layout.Place(SelectionKey, SurfaceAnchor::Home, home, {}, bounds, kSpaceStepDp * dp);
        publish(*selection, autoHeight, kSelectionLeft, kSelectionTop, kSelectionWidth, kSelectionHeight);
    }

    follow(PlacedExplorerKey, ExplorerKey);
    if (!ExplorerKey.empty())
    {
        std::vector<Rect2d> taken = instruments;
        if (selection.has_value())
            taken.push_back(*selection);
        const float top = crumbs.has_value() ? crumbs->Max().Y + gap : bounds.Min().Y;
        const Rect2d home = ExplorerKey.ends_with(".outline")
            ? Rect2d(bounds.Min().X, top, 340.0f * dp, std::max(0.0f, std::min(600.0f * dp, floor - top)))
            : Rect2d(bounds.Min().X, floor - 300.0f * dp, std::min(640.0f * dp, bounds.Width()), 300.0f * dp);
        const Rect2d placed = Layout.Place(ExplorerKey, SurfaceAnchor::FreeSpace, home, taken, bounds, kSpaceStepDp * dp);
        publish(placed, false, kExplorerLeft, kExplorerTop, kExplorerWidth, kExplorerHeight);
    }
    (void)ui.SetValue(Screen, kFront, UiValue(FrontSurface));
}

void ShellWindow::PublishLocation(const IWorkspace* workspace)
{
    const WorkspaceLocationFacet location = workspace != nullptr ? workspace->Location() : WorkspaceLocationFacet{};
    std::vector<UiRow> crumbs;
    for (const WorkspaceLocationPart& part : location.Parts)
    {
        UiRow crumb{ part.Label, "", "" };
        crumb.Key = part.Key;
        crumb.Selected = part.Selection;
        crumb.Expanded = part.Selection && SelectionShown == SelectionView::Drawer;
        crumbs.push_back(std::move(crumb));
    }
    (void)Services.Ui.SetRows(Screen, kCrumbs, crumbs);
    (void)Services.Ui.SetValue(Screen, kMapAction, UiValue(location.MapAction));
    const std::string chord =
        location.Hierarchy.empty() ? std::string{} : Services.Catalog.ChordText(HierarchyAction(*workspace, location));
    (void)Services.Ui.SetValue(Screen, kHierarchyChord, UiValue(chord));
}

std::string ShellWindow::HierarchyAction(const IWorkspace& workspace, const WorkspaceLocationFacet& location) const
{
    for (const WorkspaceExplorerInfo& explorer : workspace.Explorers())
        if (explorer.Id == location.Hierarchy)
            return explorer.Action;
    return {};
}

void ShellWindow::RevealLocation(std::int64_t index)
{
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    if (workspace == nullptr || index < 0)
        return;
    const WorkspaceLocationFacet location = workspace->Location();
    if (static_cast<std::size_t>(index) >= location.Parts.size())
        return;
    // The selection's own part opens what is selected; the parts before it
    // are places, and open the hierarchy there.
    if (location.Parts[static_cast<std::size_t>(index)].Selection)
    {
        if (SelectionShown == SelectionView::Drawer)
            Transients.Close(DrawerTransient);
        else
            (void)Services.Actions.Invoke("selection.details");
        return;
    }
    if (location.Hierarchy.empty())
        return;
    if (ExplorerShown != location.Hierarchy || ExplorerTransient == 0)
        ShowExplorer(location.Hierarchy);
    FrontSurface = kExplorerSurface;
    const std::string& key = location.Parts[static_cast<std::size_t>(index)].Key;
    if (!key.empty())
        workspace->ChooseInExplorer(location.Hierarchy, key, ExplorerChoice::Reveal, 0);
}

void ShellWindow::ShowDrawer()
{
    SelectionSheet.Drop();
    if (SelectionShown == SelectionView::Drawer)
        return;
    SelectionShown = SelectionView::Drawer;
    FrontSurface = kSelectionSurface;
    DrawerTransient = Transients.Open({ .Owner = "shell", .Dismiss = TransientDismiss::Explicit });
    TakeEscape();
}

void ShellWindow::Choose(const std::string& key)
{
    const Popover chosenFrom = Showing;
    OpenPopover(Popover::None);
    if (chosenFrom == Popover::Problems)
    {
        RevealProblem(key);
        return;
    }
    if (chosenFrom == Popover::Choices)
    {
        if (IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation))
        {
            PropertySheet& sheet = ChoicesSheet == "tool" ? ToolSheet : SelectionSheet;
            const std::vector<PropertySection> sections = SheetSections(ChoicesSheet, *workspace);
            if (!ChoicesVerb.empty())
                ActOnSheet(sheet, sections, ChoicesRow, ChoicesVerb, key);
            else
                (void)sheet.Set(sections, ChoicesRow, key);
        }
        return;
    }
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
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);

    const WorkspaceDocumentFacet document = workspace != nullptr ? workspace->Document() : WorkspaceDocumentFacet{};
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
    PublishTabs();

    (void)ui.SetValue(Screen, kProblemCount,
                      UiValue(static_cast<std::int64_t>(Services.Problems.Count(ProblemSeverity::Warning))));
    (void)ui.SetValue(Screen, kProblemErrors, UiValue(Services.Problems.Count(ProblemSeverity::Error) > 0));

    const WorkspaceTransportFacet transport = workspace != nullptr ? workspace->Transport() : WorkspaceTransportFacet{};
    (void)ui.SetValue(Screen, kTransportState, UiValue(std::string(TransportName(transport.State))));
    (void)ui.SetValue(Screen, kTransportText, UiValue(transport.Readout));
    (void)ui.SetValue(Screen, kCookEnabled, UiValue(Services.Actions.Query("transport.cook").Enabled));
    (void)ui.SetValue(Screen, kPlayEnabled, UiValue(Services.Actions.Query("transport.play").Enabled));
    (void)ui.SetValue(Screen, kStopEnabled, UiValue(Services.Actions.Query("transport.stop").Enabled));

    PublishStation(workspace);
    PublishReadouts(workspace);
    PublishHolding(workspace);
    PublishSelection(workspace);
    PublishLocation(workspace);
    PublishExplorer(workspace);
    PlaceFloatingSurfaces();

    std::vector<UiRow> notices;
    for (const Notice& notice : Services.Notices.Showing(now))
        notices.push_back(UiRow{ notice.Text, "", NoticeToneName(notice.Tone) });
    (void)ui.SetRows(Screen, kNotices, notices);

    PublishEntries();
}

void ShellWindow::PublishEntries()
{
    UiService& ui = Services.Ui;
    std::string anchor;
    std::vector<UiRow> entries;
    switch (Showing)
    {
    case Popover::Hatch:      anchor = "hatch";          entries = HatchEntries();                   break;
    case Popover::Workspaces: anchor = "tab_new";        entries = WorkspaceEntries();               break;
    case Popover::Tools:      anchor = "station";        entries = ToolEntries();                    break;
    case Popover::Category:   anchor = "cat_" + PopoverCategory; entries = CategoryEntries(PopoverCategory); break;
    case Popover::Problems:
        anchor = "problems";
        for (const Problem* problem : Services.Problems.All())
        {
            UiRow entry{ problem->Message, "", "" };
            entry.Key = std::to_string(entries.size());
            entries.push_back(std::move(entry));
        }
        break;
    case Popover::Choices:
        anchor = ChoicesSheet == "tool" ? "tool_card" : "selection_card";
        for (std::size_t i = 0; i < ChoicesListed.size(); ++i)
        {
            const std::string& choice = ChoicesListed[i];
            if (!TextFilterMatch(choice, ChoicesFilter))
                continue;
            UiRow entry{ choice.empty() ? std::string("None") : choice, "",
                         i < ChoicesNotes.size() ? ChoicesNotes[i] : std::string{} };
            entry.Key = choice;
            entries.push_back(std::move(entry));
        }
        break;
    case Popover::None:       break;
    }
    (void)ui.SetValue(Screen, kPopover, UiValue(PopoverName()));
    (void)ui.SetValue(Screen, kPopoverFilters, UiValue(Showing == Popover::Choices && ChoicesListed.size() > kChoicesUnfiltered));
    double x = 0.0;
    bool rises = false;
    std::string top = "auto";
    if (!anchor.empty())
        if (const std::optional<UiElementBox> box = ui.MeasureElement(Screen, anchor))
        {
            x = std::floor(box->X);
            // From an instrument in the lower half, a popover rises instead of
            // hanging below it.
            rises = box->Y > static_cast<float>(Services.Window.GetExtent().Height) / 2.0f;
            if (!rises)
                top = std::format("{}px", std::floor(box->Y + box->Height) + 4.0f);
        }
    (void)ui.SetValue(Screen, kPopoverRises, UiValue(rises));
    (void)ui.SetValue(Screen, kPopoverTop, UiValue(top));
    // Kept inside the window: an instrument at the right edge opens leftward.
    if (const std::optional<UiElementBox> popover = ui.MeasureElement(Screen, "popover"))
    {
        const double room = static_cast<double>(Services.Window.GetExtent().Width) - popover->Width - 8.0;
        x = std::max(0.0, std::min(x, room));
    }
    (void)ui.SetValue(Screen, kPopoverX, UiValue(x));
    (void)ui.SetRows(Screen, kEntries, entries);
}

void ShellWindow::PublishTabs()
{
    ShownTabs = AssetTabsIn(Services.Workspaces, Services.Presentation);
    std::vector<UiRow> rows;
    for (const AssetTab& tab : ShownTabs)
    {
        UiRow row{ tab.Label, tab.KindLabel, tab.Changed ? "changed" : "" };
        row.Selected = tab.Current;
        rows.push_back(std::move(row));
    }
    (void)Services.Ui.SetRows(Screen, kTabs, rows);
}

void ShellWindow::HandleTabAction(const UiAction& action)
{
    const std::int64_t index = action.Arguments.empty() ? -1 : action.Arguments[0].AsInt();
    if (index < 0 || static_cast<std::size_t>(index) >= ShownTabs.size())
        return;
    const AssetTab tab = ShownTabs[static_cast<std::size_t>(index)];
    if (action.Id == kTabDropAction)
    {
        // Let go well below the rail, the tab's editor takes a window of its own.
        const float y = action.Arguments.size() >= 3 ? static_cast<float>(action.Arguments[2].AsFloat()) : 0.0f;
        if (y > RailHeight() * 3.0f)
            Services.Workspaces.Request({ WorkspaceAction::Detach, tab.Kind });
        return;
    }
    Services.Workspaces.Request({ WorkspaceAction::Activate, tab.Kind });
    if (!tab.Document.empty())
        if (IWorkspace* workspace = Services.Workspaces.Find(tab.Kind))
            workspace->ShowDocument(tab.Document);
}

std::vector<UiRow> ShellWindow::CategoryEntries(std::string_view category) const
{
    std::vector<UiRow> rows;
    for (const ActionDefinition* action : Services.Catalog.All())
    {
        if (action->Category != category)
            continue;
        const ActionContextStack::State state = Services.Actions.Query(action->Id);
        if (!state.Claimed || !state.Enabled)
            continue;
        UiRow row{ action->Label, Services.Catalog.ChordText(action->Id), "" };
        row.Key = action->Id;
        row.Icon = std::string(IconName(action->Icon));
        row.Selected = state.Checked;
        rows.push_back(std::move(row));
    }
    return rows;
}

void ShellWindow::PublishReadouts(const IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    const WorkspaceManipulationFacet hands = workspace != nullptr ? workspace->Manipulation() : WorkspaceManipulationFacet{};
    (void)ui.SetValue(Screen, kElement, UiValue(hands.Element));
    (void)ui.SetValue(Screen, kGizmoMode, UiValue(hands.Mode));
    (void)ui.SetValue(Screen, kGizmoSpace, UiValue(hands.Space));
    (void)ui.SetValue(Screen, kGrid, UiValue(hands.Grid));
    (void)ui.SetValue(Screen, kSnap, UiValue(hands.Snap));
    (void)ui.SetValue(Screen, kPivot, UiValue(hands.Pivot));

    const WorkspaceViewFacet seeing = workspace != nullptr ? workspace->Viewing() : WorkspaceViewFacet{};
    (void)ui.SetValue(Screen, kProjection, UiValue(seeing.Projection));
    (void)ui.SetValue(Screen, kViewCount, UiValue(static_cast<std::int64_t>(seeing.Views)));
}

std::vector<PropertySection> ShellWindow::SheetSections(std::string_view sheet, IWorkspace& workspace)
{
    if (sheet != "tool")
        return SelectionSections(workspace.Selection());
    if (IPropertySource* parameters = workspace.Operation().Parameters)
        return { PropertySection{ .Heading = {}, .Source = parameters } };
    return {};
}

std::vector<PropertySection> ShellWindow::SelectionSections(const WorkspaceSelectionFacet& selection) const
{
    std::vector<PropertySection> sections = selection.Sections;
    if (SelectionShown == SelectionView::Drawer || selection.Primary)
        sections.insert(sections.end(), selection.Details.begin(), selection.Details.end());
    return sections;
}

void ShellWindow::PublishSelection(IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    const WorkspaceSelectionFacet selection = workspace != nullptr ? workspace->Selection() : WorkspaceSelectionFacet{};
    // A field being typed into describes what was selected; once something
    // else is, what it holds no longer applies to anything.
    if (selection.Title != ShownSelection)
    {
        SelectionSheet.Drop();
        SheetTyping.clear();
        ShownSelection = selection.Title;
        // A drawer is about what was selected: nothing selected, nothing to show.
        if (selection.Title.empty() && DrawerTransient != 0)
            Transients.Close(DrawerTransient);
    }
    // While the title is being typed into, what it shows is what is typed.
    if (!TitleHeld)
        (void)ui.SetValue(Screen, kSelectionTitle, UiValue(selection.Title));
    (void)ui.SetValue(Screen, kSelectionSubtitle, UiValue(selection.Subtitle));
    (void)ui.SetValue(Screen, kSelectionNamed, UiValue(selection.TitleProperty.Source != nullptr
                                                       && !selection.TitleProperty.Key.empty()));
    (void)ui.SetValue(Screen, kSelectionStaged, UiValue(selection.Staged));
    (void)ui.SetValue(Screen, kSheetTyping, UiValue(SheetTyping));
    const bool drawer = SelectionShown == SelectionView::Drawer;
    // Properties that are the work itself are shown whole, as a sheet.
    const bool sheet = selection.Primary;
    const bool shown =
        !selection.Title.empty() && (!selection.OnRequest || drawer || sheet || selection.Staged);
    (void)ui.SetValue(Screen, kSelectionShown, UiValue(shown));
    if (SelectionKey.empty() && shown)
        FrontSurface = kSelectionSurface;
    SelectionKey = !shown ? std::string{}
                 : std::format("{}.{}", kSelectionSurface, sheet ? "sheet" : drawer ? "drawer" : "card");
    (void)ui.SetValue(Screen, kDrawer, UiValue(drawer));
    (void)ui.SetValue(Screen, kSheet, UiValue(sheet));
    if (!SelectionSheet.Holding())
        (void)ui.SetRows(Screen, kSelectionRows, SelectionSheet.Present(SelectionSections(selection), drawer || sheet));

    // A selection looked at through the context wheel has its actions there;
    // the sheet keeps only what applies or cancels a staged edit.
    std::vector<UiRow> verbs;
    for (const std::string& id : !selection.OnRequest || selection.Staged ? selection.Actions
                                                                          : std::vector<std::string>{})
    {
        const ActionDefinition* definition = Services.Catalog.Find(id);
        const ActionContextStack::State state = Services.Actions.Query(id);
        if (definition == nullptr || !state.Claimed || !state.Enabled)
            continue;
        UiRow verb{ definition->Label, Services.Catalog.ChordText(id), "" };
        verb.Key = id;
        verbs.push_back(std::move(verb));
    }
    (void)ui.SetRows(Screen, kSelectionVerbs, verbs);
}

void ShellWindow::RenameSelection()
{
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    if (workspace == nullptr)
        return;
    const PropertyRef name = workspace->Selection().TitleProperty;
    const UiValue typed = Services.Ui.GetValue(Screen, kSelectionTitle);
    if (name.Source == nullptr || typed.Kind() != UiValueKind::String)
        return;
    (void)PropertyEditSession{}.Set(*name.Source, name.Key, typed.AsString());
}

void ShellWindow::PublishHolding(IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    std::vector<WorkspaceResourceFacet> roles = workspace != nullptr ? workspace->Resources()
                                                                     : std::vector<WorkspaceResourceFacet>{};
    const auto primary = std::ranges::find_if(roles, &WorkspaceResourceFacet::Primary);
    const WorkspaceResourceFacet held = primary != roles.end() ? std::move(*primary) : WorkspaceResourceFacet{};
    (void)ui.SetValue(Screen, kHoldingRole, UiValue(held.Role));
    (void)ui.SetValue(Screen, kHoldingLabel, UiValue(held.Active.Label));
    (void)ui.SetValue(Screen, kHoldingImage, UiValue(held.Active.Image));
    (void)ui.SetValue(Screen, kHoldingExplorer, UiValue(held.Explorer));

    std::vector<UiRow> stash;
    for (std::size_t slot = 0; slot < held.Stash.size(); ++slot)
    {
        UiRow row{ held.Stash[slot].Label, held.Stash[slot].Image, "" };
        row.Key = std::format("resource.slot.{}", slot + 1);
        stash.push_back(std::move(row));
    }
    (void)ui.SetRows(Screen, kStash, stash);
}

std::vector<UiRow> ShellWindow::HatchEntries() const
{
    // Everything this window can do right now, by category: the menu, with no
    // menu bar. Categories keep the order their first action was defined in,
    // which puts the application's own File and Edit first.
    std::vector<std::string> categories;
    for (const ActionDefinition* action : Services.Catalog.All())
        if (std::find(categories.begin(), categories.end(), action->Category) == categories.end())
            categories.push_back(action->Category);

    std::vector<UiRow> rows;
    for (const std::string& category : categories)
    {
        std::vector<UiRow> entries = CategoryEntries(category);
        if (entries.empty())
            continue;
        rows.push_back(Heading(category));
        for (UiRow& entry : entries)
            rows.push_back(std::move(entry));
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

std::vector<UiRow> ShellWindow::ToolEntries()
{
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    std::vector<UiRow> rows;
    if (workspace == nullptr)
        return rows;
    for (const OperationChoice& choice : workspace->Operation().Choices)
    {
        UiRow row{ choice.Label, Services.Catalog.ChordText(choice.Action), "" };
        row.Key = choice.Action;
        row.Icon = std::string(IconName(choice.Icon));
        row.Selected = choice.Current;
        rows.push_back(std::move(row));
    }
    return rows;
}

void ShellWindow::PublishStation(IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    WorkspaceOperationFacet operation = workspace != nullptr ? workspace->Operation() : WorkspaceOperationFacet{};
    const bool holding = ToolSheet.Holding();
    std::vector<UiRow> rows =
        operation.Parameters != nullptr && !holding ? ToolSheet.Present(*operation.Parameters) : std::vector<UiRow>{};
    (void)ui.SetValue(Screen, kStationName, UiValue(operation.Name));
    (void)ui.SetValue(Screen, kStationVariant, UiValue(operation.Variant));
    (void)ui.SetValue(Screen, kStationIcon, UiValue(std::string(IconName(operation.Icon))));
    (void)ui.SetValue(Screen, kStationHint, UiValue(operation.Hint));
    (void)ui.SetValue(Screen, kDraft, UiValue(std::string(DraftName(operation.Draft))));
    // The card is up while there is something on it beyond a hint: settings,
    // or drafted work to apply or cancel.
    if (holding)
        return;
    ToolCardUp = !rows.empty() || operation.Draft != ToolDraft::None;
    (void)ui.SetValue(Screen, kCardOpen, UiValue(ToolCardUp));
    (void)ui.SetRows(Screen, kToolRows, rows);
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
    regions.CaptionEnabled = Showing == Popover::None && !Prompt->IsShowing();
    Services.Window.SetFrameRegions(regions);
}
