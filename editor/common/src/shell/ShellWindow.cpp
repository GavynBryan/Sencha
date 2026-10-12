#include "shell/ShellWindow.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionOffer.h"
#include "icons/IconNames.h"
#include "input/SdlEventTranslation.h"
#include "notices/NoticeQueue.h"
#include "problems/ProblemList.h"
#include "project/AssetKinds.h"
#include "properties/PropertyEditSession.h"
#include "shell/ColorPicker.h"
#include "ui/DocumentLengths.h"
#include "ui/RadialMenuScreen.h"
#include "ui/ScreenModel.h"
#include "ui/UiScaleChoice.h"
#include "workspaces/ExplorerTable.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

#include <input/PlatformEventRouter.h>
#include <platform/SdlWindow.h>
#include <platform/WindowFrameHit.h>
#include <ui/UiAction.h>
#include <ui/UiService.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
// The floating surfaces a header can move, as a document names them.
constexpr std::string_view kExplorerSurface = "explorer";
constexpr std::string_view kSelectionSurface = "selection";

// The floating surfaces' gap from the window and the instruments, the step
// free space is searched on, and the smallest a person can size one, in dp.
constexpr float kSurfaceGapDp = 8.0f;
constexpr float kSpaceStepDp = 16.0f;
constexpr float kSurfaceMinimumWidthDp = 220.0f;
constexpr float kSurfaceMinimumHeightDp = 140.0f;
// Where each floating surface opens, in dp: the selection's card, drawer and
// sheet; the explorer as an outline down the left or a tray along the bottom.
constexpr float kCardWidthDp = 300.0f;
constexpr float kCardHeightDp = 200.0f;
constexpr float kDrawerWidthDp = 360.0f;
constexpr float kSheetWidthDp = 760.0f;
constexpr float kSheetTopDp = 36.0f;
constexpr float kOutlineWidthDp = 340.0f;
constexpr float kOutlineHeightDp = 600.0f;
constexpr float kTrayWidthDp = 640.0f;
constexpr float kTrayHeightDp = 300.0f;

// The rail's height in dp, when the document has not laid out to say.
constexpr float kRailHeightDp = 34.0f;

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

} // namespace

ShellWindow::ShellWindow(ShellWindowServices services)
    : Services(std::move(services))
{
    const WindowExtent extent = Services.Window.GetExtent();
    SurfaceId = Services.Ui.CreateSurface("shell", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(SurfaceId, Services.Window.GetId());

    ContentSurfaceId = Services.Ui.CreateSurface("content", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(ContentSurfaceId, Services.Window.GetId());
    Services.Ui.SetSurfaceLayer(ContentSurfaceId, kContentUiLayer);
    Services.Ui.SetSurfaceInputPolicy(ContentSurfaceId, UiSurfaceInputPolicy::Disabled);

    // Created after the shell's surface, so it draws over it.
    OverlaySurfaceId = Services.Ui.CreateSurface("overlay", RenderExtent{ extent.Width, extent.Height });
    Services.Ui.SetSurfaceWindow(OverlaySurfaceId, Services.Window.GetId());
    Services.Ui.SetSurfaceInputPolicy(OverlaySurfaceId, UiSurfaceInputPolicy::Disabled);

    ColorPick = std::make_unique<ColorPicker>(Services.Ui, SurfaceId);
    Palette = std::make_unique<CommandPalette>(Services.Ui, SurfaceId, Services.Catalog, Services.Actions);
    AssetShelf = std::make_unique<Shelf>(Services, SurfaceId);
    Sheets = std::make_unique<SheetPanels>(SheetPanelsHost{
        .Ui = Services.Ui,
        .Catalog = Services.Catalog,
        .Actions = Services.Actions,
        .Notices = Services.Notices,
        .Assets = *AssetShelf,
        .Sections = [this](SheetId sheet) { return SheetSections(sheet); },
        .OpenChoices = [this](SheetPoint at, const PropertyVerb& verb) { Menus->ShowChoices(at, verb); },
        .OpenColor = [this](SheetPoint at) { OpenColor(at); },
        .TakePress = [this] { return Services.Pointer.HandGestureToApplication(Services.Window.GetId()); },
        .CapturePointer = [this](bool held) { SetRelativeMouseMode(Services.Window, held); },
    });
    Explorer = std::make_unique<ExplorerPanel>(ExplorerPanelHost{
        .Ui = Services.Ui,
        .Workspaces = Services.Workspaces,
        .Presentation = Services.Presentation,
        .Catalog = Services.Catalog,
        .Actions = Services.Actions,
        .WindowActions = WindowActions,
        .Transients = Transients,
        .Assets = *AssetShelf,
        .TakeEscape = [this] { TakeEscape(); },
        .BringToFront = [this] { FrontSurface = kExplorerSurface; },
    });
    Menus = std::make_unique<RailMenus>(RailMenusHost{
        .Ui = Services.Ui,
        .Catalog = Services.Catalog,
        .Actions = Services.Actions,
        .Workspaces = Services.Workspaces,
        .Presentation = Services.Presentation,
        .Problems = Services.Problems,
        .Transients = Transients,
        .Window = Services.Window,
        .TakeEscape = [this] { TakeEscape(); },
        .RevealProblem = [this](std::string_view index) { RevealProblem(index); },
        .ChooseForSheet = [this](SheetPoint at, std::string_view verb, std::string_view choice) {
            Sheets->Act(at, verb, choice);
        },
    });

    ScreenModel model("asset://shell.rml", "shell");
    Declare(model);
    Sheets->Declare(model);
    Explorer->Declare(model);
    Menus->Declare(model);
    Screen = Services.Ui.OpenScreen(SurfaceId, model.Description());
    Sheets->Open(Screen);
    Explorer->Open(Screen);
    Menus->Open(Screen);

    Transients.SetClosedObserver([this](TransientSurfaces::Id id) {
        if (Menus->Closed(id))
            ColorPick->Close();
        (void)Explorer->Closed(id);
        if (id == DrawerTransient)
        {
            DrawerTransient = 0;
            SelectionShown = SelectionView::Card;
        }
        if (id == ShelfTransient)
        {
            ShelfTransient = 0;
            if (AssetShelf->IsRaised())
                AssetShelf->Hide();
        }
        if (!Transients.AnyOpen())
            EscapeLayer.Reset();
    });
    // Escape belongs to what is open while anything is.
    EscapeActions.Claim("edit.escape", ActionBinding{ .Invoke = [this] { EscapeFront(); } });

    OfferWindowActions();
    WindowLayer = Services.Actions.Push(WindowActions, ActionRank::Window);
    Deck = std::make_unique<TimelineDeck>(Services.Ui, SurfaceId, Services.Workspaces, Services.Actions,
                                          Services.Presentation);
    MakeClickWheel(FileWheel, AssetShelf->FileWheel());
    MakeClickWheel(NewWheel, AssetShelf->NewWheel());
    WheelScreen = std::make_unique<RadialMenuScreen>(Services.Ui);
    WheelScreen->MoveTo(OverlaySurfaceId);
    WheelScreen->SetShown(true);
    Prompt = std::make_unique<PromptDialog>(Services.Ui, SurfaceId, Services.Prompts, Services.Actions);
}

ShellWindow::~ShellWindow()
{
    WindowLayer.Reset();
    EscapeLayer.Reset();
    Prompt.reset();
    WheelScreen.reset();
    FileWheel.Session.reset();
    NewWheel.Session.reset();
    Menus.reset();
    Explorer.reset();
    Sheets.reset();
    AssetShelf.reset();
    Deck.reset();
    ColorPick.reset();
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

void ShellWindow::Declare(ScreenModel& model)
{
    const auto text = [&model](std::string name, std::string initial = {}) {
        return model.Property(std::move(name), UiValue(std::move(initial)));
    };
    const auto flag = [&model](std::string name, bool initial = false) {
        return model.Property(std::move(name), UiValue(initial));
    };
    Ids.ProblemCount = model.Property("problem_count", UiValue(std::int64_t{ 0 }));
    Ids.ProblemErrors = flag("problem_errors");
    Ids.TransportState = text("transport_state", "none");
    Ids.TransportText = text("transport_text");
    Ids.CookEnabled = flag("cook_enabled");
    Ids.PlayEnabled = flag("play_enabled");
    Ids.StopEnabled = flag("stop_enabled");
    Ids.StationName = text("station_name");
    Ids.StationVariant = text("station_variant");
    Ids.StationIcon = text("station_icon");
    Ids.StationHint = text("station_hint");
    Ids.Draft = text("draft", "none");
    Ids.CardOpen = flag("card_open");
    Ids.Element = text("element");
    Ids.GizmoMode = text("gizmo_mode");
    Ids.GizmoSpace = text("gizmo_space");
    Ids.Grid = text("grid");
    Ids.Snap = flag("snap");
    Ids.Pivot = flag("pivot");
    Ids.Projection = text("projection");
    Ids.ViewCount = model.Property("view_count", UiValue(std::int64_t{ 0 }));
    Ids.HoldingRole = text("holding_role");
    Ids.HoldingLabel = text("holding_label");
    Ids.HoldingImage = text("holding_image");
    Ids.HoldingExplorer = text("holding_explorer");
    Ids.Motion = flag("motion", true);
    Ids.SelectionTitle = text("selection_title");
    Ids.SelectionStaged = flag("selection_staged");
    Ids.Drawer = flag("drawer");
    Ids.SelectionSubtitle = text("selection_subtitle");
    Ids.Sheet = flag("sheet");
    Ids.ExplorerLeft = text("explorer_left", "0px");
    Ids.ExplorerTop = text("explorer_top", "0px");
    Ids.ExplorerWidth = text("explorer_width", "auto");
    Ids.ExplorerHeight = text("explorer_height", "auto");
    Ids.SelectionLeft = text("selection_left", "0px");
    Ids.SelectionTop = text("selection_top", "0px");
    Ids.SelectionWidth = text("selection_width", "auto");
    Ids.SelectionHeight = text("selection_height", "auto");
    Ids.Front = text("front");
    Ids.HierarchyChord = text("hierarchy_chord");
    Ids.SelectionShown = flag("selection_shown");
    Ids.MapAction = text("map_action");
    Ids.SelectionNamed = flag("selection_named");
    Ids.InstrumentsLift = text("instruments_lift", "8dp");
    Ids.Notices = model.Rows("notices");
    Ids.Stash = model.Rows("stash");
    Ids.SelectionVerbs = model.Rows("selection_verbs");
    Ids.Crumbs = model.Rows("crumbs");
    Ids.Tabs = model.Rows("tabs");

    const auto member = [this](void (ShellWindow::*handle)(const UiAction&)) {
        return [this, handle](const UiAction& action) { (this->*handle)(action); };
    };
    const auto move = [this](SurfaceMove step) {
        return [this, step](const UiAction& action) { MoveSurface(action, step); };
    };
    Own(model, "shell_invoke", member(&ShellWindow::OnInvoke));
    Own(model, "shell_window", member(&ShellWindow::OnWindowKey));
    Own(model, "shell_drawer", member(&ShellWindow::OnDrawer));
    Own(model, "shell_title_hold", member(&ShellWindow::OnTitleHold));
    Own(model, "shell_title_release", member(&ShellWindow::OnTitleRelease));
    Own(model, "shell_grab", move(SurfaceMove::Grab));
    Own(model, "shell_drag", move(SurfaceMove::Drag));
    Own(model, "shell_drop", move(SurfaceMove::Drop));
    Own(model, "shell_unmove", move(SurfaceMove::Reset));
    Own(model, "shell_raise", move(SurfaceMove::Raise));
    Own(model, "shell_tab", member(&ShellWindow::OnTab));
    Own(model, "shell_tab_drop", member(&ShellWindow::OnTabDrop));
    Own(model, "shell_crumb", member(&ShellWindow::OnCrumb));
}

void ShellWindow::Own(ScreenModel& model, std::string name, std::function<void(const UiAction&)> handle)
{
    OwnActions.push_back({ model.Action(std::move(name)), std::move(handle) });
}

void ShellWindow::OfferWindowActions()
{
    OfferAction(Services.Catalog, WindowActions,
                { .Id = "shell.palette", .Label = "Command Palette", .Category = "View",
                  .DefaultChords = { KeyChord{ SDLK_K, { .Ctrl = true } } } },
                { .Invoke = [this] {
                    Menus->Show(RailMenus::Popover::None);
                    Palette->Open();
                } });
    OfferAction(Services.Catalog, WindowActions, DetailsAction(),
                { .Invoke = [this] { ShowDrawer(); }, .Enabled = [this] { return !ShownSelection.empty(); } });
    OfferAction(Services.Catalog, WindowActions,
                { .Id = "shelf.toggle", .Label = "Asset Shelf", .Category = "View", .Icon = IconId::Folder,
                  .DefaultChords = { KeyChord{ SDLK_SPACE, { .Shift = true } } } },
                { .Invoke = [this] { AssetShelf->Toggle(); } });

    // What this window does with the editor it shows.
    const auto active = [this] { return Services.Workspaces.ActiveKindIn(Services.Presentation); };
    const auto request = [this, active](WorkspaceAction action) {
        if (const WorkspaceKind* kind = active())
            Services.Workspaces.Request({ action, kind->Id });
    };
    const bool main = Services.Presentation == Services.Workspaces.MainWindow();
    OfferAction(Services.Catalog, WindowActions,
                { .Id = "workspace.detach", .Label = "Move to New Window", .Category = "Workspace" },
                { .Invoke = [request] { request(WorkspaceAction::Detach); },
                  .Enabled = [active, main] { return main && active() != nullptr; } });
    OfferAction(Services.Catalog, WindowActions,
                { .Id = "workspace.attach", .Label = "Move to Main Window", .Category = "Workspace" },
                { .Invoke = [request] { request(WorkspaceAction::Attach); },
                  .Enabled = [active, main] { return !main && active() != nullptr; } });
    OfferAction(Services.Catalog, WindowActions,
                { .Id = "workspace.close", .Label = "Close Editor", .Category = "Workspace" },
                { .Invoke = [request] { request(WorkspaceAction::Close); },
                  .Enabled = [active] { return active() != nullptr; } });
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
    (void)Services.Ui.SetValue(Screen, Ids.Motion, UiValue(preferences.Motion));
    for (const UiSurfaceId surface : { SurfaceId, ContentSurfaceId, OverlaySurfaceId })
    {
        Services.Ui.SetSurfaceSize(surface, RenderExtent{ extent.Width, extent.Height });
        Services.Ui.SetSurfaceScale(surface, scale);
    }

    // Laid out last frame; measured once and read by everything placed against it.
    Rail = Services.Ui.MeasureElement(Screen, "rail");
    IWorkspace* active = Services.Workspaces.ActiveIn(Services.Presentation);
    FollowWorkspace(active);
    if (active != nullptr)
    {
        // Pinned, the shelf takes the bottom of the window; raised, it lies over the work.
        const float rail = RailHeight();
        const float shelf = AssetShelf->IsPinned() ? AssetShelf->Reach() : 0.0f;
        active->SetContentRect(Rect2d(Vec2d(0.0f, rail),
                                      Vec2d(static_cast<float>(extent.Width),
                                            std::max(0.0f, static_cast<float>(extent.Height) - rail - shelf))));
    }
    HandleActions();
    YieldPointerToWheel();
    Prompt->Update();
    // A question stands alone: nothing else opened stays open under it.
    if (Prompt->IsShowing())
    {
        Menus->Show(RailMenus::Popover::None);
        Palette->Close();
    }
    Palette->Update();
    if (ColorPick->IsOpen() && !ColorPick->Update() && Menus->Showing() == RailMenus::Popover::Color)
        Menus->Show(RailMenus::Popover::None);
    if (Services.Role == ShellRole::Shelf)
        AssetShelf->Fill(static_cast<float>(extent.Height) - RailHeight());
    ShownTabs = AssetTabsIn(Services.Workspaces, Services.Presentation);
    AssetShelf->Update(Services.Ui.GetSurfaceScale(SurfaceId), static_cast<float>(extent.Height), ShownTabs);
    HandCarryToContent();
    if (const std::optional<ShelfDrop> drop = AssetShelf->TakeDrop())
    {
        IWorkspace* receiving = Services.Workspaces.ActiveIn(Services.Presentation);
        if (receiving == nullptr || !receiving->DropAsset(drop->File, drop->At))
            Services.Notices.Post(std::format("Nothing here takes {}", NameOf(drop->File)), NoticeTone::Warning,
                                  NoticeClockNow());
    }
    if (const std::optional<ShelfWheelRequest> wanted = AssetShelf->TakeWheelRequest(); wanted && !WheelUp())
        (void)(wanted->Wheel == ShelfWheel::File ? FileWheel : NewWheel)
            .Input.Route(PointerDownEvent{ .Position = wanted->At, .Button = MouseButton::Right, .Modifiers = {} });
    const ClickWheel* up = OpenWheel();
    WheelScreen->Present(up != nullptr ? up->Session.get() : nullptr,
                         up == &NewWheel ? &AssetShelf->NewWheel() : &AssetShelf->FileWheel());
    FollowShelf();
    if (Prompt->IsShowing())
        Deck->Hide();
    else
        Deck->Update();
    Publish(now);
    PublishCaption();
}

void ShellWindow::FollowWorkspace(const IWorkspace* workspace)
{
    if (workspace == ShownWorkspace)
        return;
    ShownWorkspace = workspace;
    Sheets->MovedOn(SheetId::Tool);
    Sheets->MovedOn(SheetId::Selection);
    Title = TitleEdit::Resting;
    Renaming = {};
}

float ShellWindow::RailHeight() const
{
    return Rail.has_value() ? Rail->Height : kRailHeightDp * Services.Ui.GetSurfaceScale(SurfaceId);
}

void ShellWindow::HandleActions()
{
    const std::vector<UiAction> actions = Services.Ui.DrainActions(Screen);
    Explorer->Handle(actions);
    for (const UiAction& action : actions)
    {
        if (Menus->Handle(action) || Sheets->Handle(action))
            continue;
        const auto own = std::ranges::find(OwnActions, action.Id, &OwnAction::Id);
        if (own != OwnActions.end())
            own->Handle(action);
    }
}

void ShellWindow::OnInvoke(const UiAction& action)
{
    (void)Services.Actions.Invoke(action.Arg(0).AsString());
}

void ShellWindow::OnWindowKey(const UiAction& action)
{
    const std::string_view key = action.Arg(0).AsString();
    if (key == "minimize")
        (void)Services.Window.Minimize();
    else if (key == "maximize")
        (void)(Services.Window.IsMaximized() ? Services.Window.Restore() : Services.Window.Maximize());
    else if (key == "close" && Services.CloseWindow)
        Services.CloseWindow();
}

void ShellWindow::OnDrawer(const UiAction&)
{
    if (SelectionShown == SelectionView::Drawer)
        Transients.Close(DrawerTransient);
    else
        ShowDrawer();
}

void ShellWindow::OnTitleHold(const UiAction&)
{
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    Renaming = workspace != nullptr ? workspace->Selection().TitleProperty : PropertyRef{};
    Title = TitleEdit::Held;
}

void ShellWindow::OnTitleRelease(const UiAction&)
{
    if (Title != TitleEdit::Held)
        return;
    Title = TitleEdit::Resting;
    const PropertyRef name = std::exchange(Renaming, {});
    const UiValue typed = Services.Ui.GetValue(Screen, Ids.SelectionTitle);
    if (name.Source != nullptr && typed.Kind() == UiValueKind::String)
        (void)PropertyEditSession{}.Set(*name.Source, name.Key, typed.AsString());
}

void ShellWindow::MoveSurface(const UiAction& action, SurfaceMove move)
{
    const std::string surface(action.Arg(0).AsString());
    const bool explorer = surface == kExplorerSurface;
    if (!explorer && surface != kSelectionSurface)
        return;
    if (move == SurfaceMove::Raise)
    {
        FrontSurface = surface;
        return;
    }
    const std::string& key = explorer ? Explorer->LayoutKey() : SelectionKey;
    if (key.empty())
        return;
    const Vec2d pointer(static_cast<float>(action.Arg(1).AsFloat()), static_cast<float>(action.Arg(2).AsFloat()));
    switch (move)
    {
    case SurfaceMove::Grab:
        FrontSurface = surface;
        if (action.Arg(3).AsString() == "corner")
            Layout.PressCorner(key, pointer);
        else
            Layout.PressHeader(key, pointer);
        break;
    case SurfaceMove::Drag:
    {
        const float dp = Services.Ui.GetSurfaceScale(SurfaceId);
        Layout.Drag(pointer, FloatingBounds(), Vec2d(kSurfaceMinimumWidthDp * dp, kSurfaceMinimumHeightDp * dp));
        break;
    }
    case SurfaceMove::Drop:  Layout.Drop(); break;
    case SurfaceMove::Reset: Layout.Reset(key); break;
    case SurfaceMove::Raise: break;
    }
}

void ShellWindow::OnTab(const UiAction& action)
{
    const std::int64_t index = action.Arg(0).AsInt(-1);
    if (index < 0 || static_cast<std::size_t>(index) >= ShownTabs.size())
        return;
    const AssetTab tab = ShownTabs[static_cast<std::size_t>(index)];
    Services.Workspaces.Request({ WorkspaceAction::Activate, tab.Kind });
    if (!tab.Document.empty())
        if (IWorkspace* workspace = Services.Workspaces.Find(tab.Kind))
            workspace->ShowDocument(tab.Document);
}

void ShellWindow::OnTabDrop(const UiAction& action)
{
    const std::int64_t index = action.Arg(0).AsInt(-1);
    if (index < 0 || static_cast<std::size_t>(index) >= ShownTabs.size())
        return;
    // Let go well below the rail, the tab's editor takes a window of its own.
    if (static_cast<float>(action.Arg(2).AsFloat()) > RailHeight() * 3.0f)
        Services.Workspaces.Request({ WorkspaceAction::Detach, ShownTabs[static_cast<std::size_t>(index)].Kind });
}

void ShellWindow::OnCrumb(const UiAction& action)
{
    RevealLocation(action.Arg(0).AsInt(-1));
}

void ShellWindow::HandCarryToContent()
{
    // The world takes a carry the moment it leaves the shelf, so what follows
    // the pointer from there is the workspace's own interaction; the shelf's
    // drag ends without a drop because the carried file is taken first.
    const std::optional<ShelfDrop> carried = AssetShelf->CarriedOverContent();
    IWorkspace* active = Services.Workspaces.ActiveIn(Services.Presentation);
    if (!carried.has_value() || active == nullptr || !active->TakesCarry(carried->File))
        return;
    (void)AssetShelf->TakeCarried();
    if (!Services.Pointer.HandGestureToApplication(Services.Window.GetId()))
        return;
    if (AssetShelf->IsRaised())
        AssetShelf->Hide();
    active->TakeCarry(carried->File, carried->At);
}

bool ShellWindow::TakeHeldInput(const InputEvent& event)
{
    if (ClickWheel* wheel = OpenWheel())
        return wheel->Input.Route(event) == InputConsumed::Yes;
    return Sheets->TakeHeldInput(event);
}

ShellWindow::ClickWheel* ShellWindow::OpenWheel()
{
    for (ClickWheel* wheel : { &FileWheel, &NewWheel })
        if (wheel->Session != nullptr && wheel->Session->GetPhase() == RadialMenuPhase::Open)
            return wheel;
    return nullptr;
}

void ShellWindow::MakeClickWheel(ClickWheel& wheel, IRadialMenuModel& model)
{
    // Its entries are words: a roomier wheel, as the context wheel is.
    wheel.Session = std::make_unique<RadialMenuSession>(
        model, KeyChord{}, [this] { return WheelFrame(); }, [](Vec2d) { return true; },
        RadialMenu::Proportions{ .Radius = 112.0f, .Hub = 60.0f, .Button = 44.0f });
    RadialMenuSession* session = wheel.Session.get();
    wheel.Input.AddHandler([session](const InputEvent& event, PointerCapture& capture) {
        // The press the shelf passed on opens it; once up, it has the pointer.
        const auto* press = std::get_if<PointerDownEvent>(&event);
        if (session->GetPhase() != RadialMenuPhase::Open && press != nullptr)
        {
            session->OpenClicked(press->Position, capture);
            return InputConsumed::Yes;
        }
        return session->OnInput(event, capture);
    });
}

RadialMenu::Frame ShellWindow::WheelFrame() const
{
    const WindowExtent extent = Services.Window.GetExtent();
    return { .Scale = Services.Ui.GetSurfaceScale(SurfaceId),
             .Min = {},
             .Max = { static_cast<float>(extent.Width), static_cast<float>(extent.Height) } };
}

void ShellWindow::YieldPointerToWheel()
{
    // An open click wheel is chosen from by the next click wherever it lands,
    // so nothing of the document under it takes the pointer while it is up.
    const UiSurfaceInputPolicy policy = WheelUp() ? UiSurfaceInputPolicy::Disabled : UiSurfaceInputPolicy::Full;
    if (policy == InputPolicy)
        return;
    Services.Ui.SetSurfaceInputPolicy(SurfaceId, policy);
    InputPolicy = policy;
}

void ShellWindow::FollowShelf()
{
    // A workspace showing nothing is shown what it could open.
    const WorkspaceKind* kind = Services.Workspaces.ActiveKindIn(Services.Presentation);
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    const std::string activeKind = kind != nullptr ? kind->Id : std::string{};
    if (activeKind != ShelvedFor)
    {
        ShelvedFor = activeKind;
        const WorkspaceDocumentFacet document = workspace != nullptr ? workspace->Document() : WorkspaceDocumentFacet{};
        if (kind != nullptr && !kind->Opens.empty() && document.Label.empty() && document.Open.empty())
            AssetShelf->Show(std::string(KindOf(ProjectFile{ .Extension = kind->Opens.front().Extension }).Category));
    }

    // Raised, Escape takes it away; pinned, it stays put.
    if (AssetShelf->IsRaised() && ShelfTransient == 0)
    {
        ShelfTransient = Transients.Open();
        TakeEscape();
    }
    else if (!AssetShelf->IsRaised() && ShelfTransient != 0)
        Transients.Close(ShelfTransient);

    const float dp = Services.Ui.GetSurfaceScale(SurfaceId);
    (void)Services.Ui.SetValue(Screen, Ids.InstrumentsLift, UiValue(PixelLength(AssetShelf->Reach() + kSurfaceGapDp * dp)));
    Deck->Lift(AssetShelf->Reach());
}

void ShellWindow::OpenColor(SheetPoint at)
{
    const std::optional<UiRow> shown = Sheets->Shown(at);
    const std::optional<UiElementBox> anchor =
        Services.Ui.MeasureElement(Screen, std::format("{}_row_{}", SheetName(at.Sheet), at.Row), UiBoxArea::Border);
    if (!shown.has_value() || !shown->Editable || !anchor.has_value())
        return;
    Menus->Show(RailMenus::Popover::Color);
    ColorPick->Open({ .Source = [this, at] { return Sheets->SourceAt(at); },
                      .Key = Sheets->EditKey(at),
                      .Label = Sheets->Label(at) },
                    shown->Value, *anchor);
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
    const TransientSurfaces::Id front = FrontSurface == kExplorerSurface ? Explorer->Transient()
                                      : FrontSurface == kSelectionSurface ? DrawerTransient
                                                                          : 0;
    if (Menus->Transient() == 0 && front != 0 && Transients.IsOpen(front))
        Transients.Close(front);
    else
        (void)Transients.Escape();
}

void ShellWindow::TakeEscape()
{
    if (!EscapeLayer.IsActive())
        EscapeLayer = Services.Actions.Push(EscapeActions, ActionRank::Surface);
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
    float floor = bounds.Max().Y - AssetShelf->Reach();
    for (const std::optional<Rect2d>& instrument : { station, holding })
        if (instrument.has_value())
            floor = std::min(floor, instrument->Min().Y - gap);

    const auto publish = [&](const Rect2d& rect, bool autoHeight, UiModelPropertyId left, UiModelPropertyId top,
                             UiModelPropertyId width, UiModelPropertyId height) {
        (void)ui.SetValue(Screen, left, UiValue(PixelLength(rect.Min().X)));
        (void)ui.SetValue(Screen, top, UiValue(PixelLength(rect.Min().Y)));
        (void)ui.SetValue(Screen, width, UiValue(PixelLength(rect.Width())));
        (void)ui.SetValue(Screen, height, UiValue(autoHeight ? std::string("auto") : PixelLength(rect.Height())));
    };
    const auto follow = [&](std::string& placed, const std::string& key) {
        if (placed != key && !placed.empty())
            Layout.Hide(placed);
        placed = key;
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
            const float width = std::min(kSheetWidthDp * dp, bounds.Width());
            const float top = bounds.Min().Y + kSheetTopDp * dp;
            home = Rect2d(bounds.Center().X - width / 2.0f, top, width, std::max(0.0f, floor - top));
        }
        else if (SelectionKey.ends_with(".drawer"))
        {
            const float top = view.has_value() ? view->Max().Y + gap : bounds.Min().Y;
            home = Rect2d(bounds.Max().X - kDrawerWidthDp * dp, top, kDrawerWidthDp * dp, std::max(0.0f, floor - top));
        }
        else
        {
            const std::optional<Rect2d> card = MeasureBorder("selection_card");
            const float height = card.has_value() ? card->Height() : kCardHeightDp * dp;
            home = Rect2d(bounds.Max().X - kCardWidthDp * dp, floor - height, kCardWidthDp * dp, height);
            autoHeight = !Layout.Sized(SelectionKey);
        }
        selection = Layout.Place(SelectionKey, SurfaceAnchor::Home, home, {}, bounds, kSpaceStepDp * dp);
        publish(*selection, autoHeight, Ids.SelectionLeft, Ids.SelectionTop, Ids.SelectionWidth, Ids.SelectionHeight);
    }

    const std::string& explorerKey = Explorer->LayoutKey();
    follow(PlacedExplorerKey, explorerKey);
    if (!explorerKey.empty())
    {
        std::vector<Rect2d> taken = instruments;
        if (selection.has_value())
            taken.push_back(*selection);
        const float top = crumbs.has_value() ? crumbs->Max().Y + gap : bounds.Min().Y;
        const Rect2d home = explorerKey.ends_with(".outline")
            ? Rect2d(bounds.Min().X, top, kOutlineWidthDp * dp,
                     std::max(0.0f, std::min(kOutlineHeightDp * dp, floor - top)))
            : Rect2d(bounds.Min().X, floor - kTrayHeightDp * dp, std::min(kTrayWidthDp * dp, bounds.Width()),
                     kTrayHeightDp * dp);
        const Rect2d placed = Layout.Place(explorerKey, SurfaceAnchor::FreeSpace, home, taken, bounds, kSpaceStepDp * dp);
        publish(placed, false, Ids.ExplorerLeft, Ids.ExplorerTop, Ids.ExplorerWidth, Ids.ExplorerHeight);
    }
    (void)ui.SetValue(Screen, Ids.Front, UiValue(FrontSurface));
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
    (void)Services.Ui.SetRows(Screen, Ids.Crumbs, crumbs);
    (void)Services.Ui.SetValue(Screen, Ids.MapAction, UiValue(location.MapAction));
    const std::string chord =
        location.Hierarchy.empty() ? std::string{} : Services.Catalog.ChordText(ExplorerTable::ActionOf(location.Hierarchy));
    (void)Services.Ui.SetValue(Screen, Ids.HierarchyChord, UiValue(chord));
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
    const WorkspaceLocationPart& part = location.Parts[static_cast<std::size_t>(index)];
    if (part.Selection)
    {
        if (SelectionShown == SelectionView::Drawer)
            Transients.Close(DrawerTransient);
        else
            ShowDrawer();
        return;
    }
    if (!location.Hierarchy.empty())
        Explorer->Reveal(location.Hierarchy, part.Key);
}

void ShellWindow::ShowDrawer()
{
    Sheets->MovedOn(SheetId::Selection);
    if (SelectionShown == SelectionView::Drawer)
        return;
    SelectionShown = SelectionView::Drawer;
    FrontSurface = kSelectionSurface;
    DrawerTransient = Transients.Open();
    TakeEscape();
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

    (void)ui.SetValue(Screen, Ids.ProblemCount,
                      UiValue(static_cast<std::int64_t>(Services.Problems.Count(ProblemSeverity::Warning))));
    (void)ui.SetValue(Screen, Ids.ProblemErrors, UiValue(Services.Problems.Count(ProblemSeverity::Error) > 0));

    const WorkspaceTransportFacet transport = workspace != nullptr ? workspace->Transport() : WorkspaceTransportFacet{};
    (void)ui.SetValue(Screen, Ids.TransportState, UiValue(std::string(TransportName(transport.State))));
    (void)ui.SetValue(Screen, Ids.TransportText, UiValue(transport.Readout));
    (void)ui.SetValue(Screen, Ids.CookEnabled, UiValue(Services.Actions.Query("transport.cook").Enabled));
    (void)ui.SetValue(Screen, Ids.PlayEnabled, UiValue(Services.Actions.Query("transport.play").Enabled));
    (void)ui.SetValue(Screen, Ids.StopEnabled, UiValue(Services.Actions.Query("transport.stop").Enabled));

    PublishStation(workspace);
    PublishReadouts(workspace);
    PublishHolding(workspace);
    PublishSelection(workspace);
    PublishLocation(workspace);
    Explorer->Publish(workspace);
    PlaceFloatingSurfaces();

    std::vector<UiRow> notices;
    for (const Notice& notice : Services.Notices.Showing(now))
        notices.push_back(UiRow{ notice.Text, "", NoticeToneName(notice.Tone) });
    (void)ui.SetRows(Screen, Ids.Notices, notices);

    Menus->Publish();
}

void ShellWindow::PublishTabs()
{
    std::vector<UiRow> rows;
    for (const AssetTab& tab : ShownTabs)
    {
        UiRow row{ tab.Label, tab.KindLabel, tab.Changed ? "changed" : "" };
        row.Selected = tab.Current;
        rows.push_back(std::move(row));
    }
    (void)Services.Ui.SetRows(Screen, Ids.Tabs, rows);
}

void ShellWindow::PublishReadouts(const IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    const WorkspaceManipulationFacet hands = workspace != nullptr ? workspace->Manipulation() : WorkspaceManipulationFacet{};
    (void)ui.SetValue(Screen, Ids.Element, UiValue(hands.Element));
    (void)ui.SetValue(Screen, Ids.GizmoMode, UiValue(hands.Mode));
    (void)ui.SetValue(Screen, Ids.GizmoSpace, UiValue(hands.Space));
    (void)ui.SetValue(Screen, Ids.Grid, UiValue(hands.Grid));
    (void)ui.SetValue(Screen, Ids.Snap, UiValue(hands.Snap));
    (void)ui.SetValue(Screen, Ids.Pivot, UiValue(hands.Pivot));

    const WorkspaceViewFacet seeing = workspace != nullptr ? workspace->Viewing() : WorkspaceViewFacet{};
    (void)ui.SetValue(Screen, Ids.Projection, UiValue(seeing.Projection));
    (void)ui.SetValue(Screen, Ids.ViewCount, UiValue(static_cast<std::int64_t>(seeing.Views)));
}

std::vector<PropertySection> ShellWindow::SheetSections(SheetId sheet)
{
    IWorkspace* workspace = Services.Workspaces.ActiveIn(Services.Presentation);
    if (workspace == nullptr)
        return {};
    if (sheet == SheetId::Selection)
        return SelectionSections(workspace->Selection());
    if (IPropertySource* parameters = workspace->Operation().Parameters)
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
    // Whatever was held, typed, dragged or renamed for what was selected
    // before no longer applies to anything.
    if (selection.Revision != ShownRevision || selection.Title != ShownSelection)
    {
        Sheets->MovedOn(SheetId::Selection);
        Title = TitleEdit::Resting;
        Renaming = {};
        ShownRevision = selection.Revision;
        ShownSelection = selection.Title;
        if (Menus->Showing() == RailMenus::Popover::Color || Menus->Showing() == RailMenus::Popover::Choices)
            Menus->Show(RailMenus::Popover::None);
        // A drawer is about what was selected: nothing selected, nothing to show.
        if (selection.Title.empty() && DrawerTransient != 0)
            Transients.Close(DrawerTransient);
    }
    // While the title is being typed into, what it shows is what is typed.
    if (Title == TitleEdit::Resting)
        (void)ui.SetValue(Screen, Ids.SelectionTitle, UiValue(selection.Title));
    (void)ui.SetValue(Screen, Ids.SelectionSubtitle, UiValue(selection.Subtitle));
    (void)ui.SetValue(Screen, Ids.SelectionNamed, UiValue(selection.TitleProperty.Source != nullptr
                                                          && !selection.TitleProperty.Key.empty()));
    (void)ui.SetValue(Screen, Ids.SelectionStaged, UiValue(selection.Staged));
    const bool drawer = SelectionShown == SelectionView::Drawer;
    // Properties that are the work itself are shown whole, as a sheet.
    const bool sheet = selection.Primary;
    const bool shown =
        !selection.Title.empty() && (!selection.OnRequest || drawer || sheet || selection.Staged);
    (void)ui.SetValue(Screen, Ids.SelectionShown, UiValue(shown));
    if (SelectionKey.empty() && shown)
        FrontSurface = kSelectionSurface;
    SelectionKey = !shown ? std::string{}
                 : std::format("{}.{}", kSelectionSurface, sheet ? "sheet" : drawer ? "drawer" : "card");
    (void)ui.SetValue(Screen, Ids.Drawer, UiValue(drawer));
    (void)ui.SetValue(Screen, Ids.Sheet, UiValue(sheet));
    (void)Sheets->Present(SheetId::Selection, SelectionSections(selection), drawer || sheet);

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
    (void)ui.SetRows(Screen, Ids.SelectionVerbs, verbs);
}

void ShellWindow::PublishHolding(IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    std::vector<WorkspaceResourceFacet> roles = workspace != nullptr ? workspace->Resources()
                                                                     : std::vector<WorkspaceResourceFacet>{};
    const auto primary = std::ranges::find_if(roles, &WorkspaceResourceFacet::Primary);
    const WorkspaceResourceFacet held = primary != roles.end() ? std::move(*primary) : WorkspaceResourceFacet{};
    (void)ui.SetValue(Screen, Ids.HoldingRole, UiValue(held.Role));
    (void)ui.SetValue(Screen, Ids.HoldingLabel, UiValue(held.Active.Label));
    (void)ui.SetValue(Screen, Ids.HoldingImage, UiValue(held.Active.Image));
    (void)ui.SetValue(Screen, Ids.HoldingExplorer, UiValue(held.Explorer));

    std::vector<UiRow> stash;
    for (const HeldResource& slot : held.Stash)
    {
        UiRow row{ slot.Label, slot.Image, "" };
        row.Key = slot.Action;
        stash.push_back(std::move(row));
    }
    (void)ui.SetRows(Screen, Ids.Stash, stash);
}

void ShellWindow::PublishStation(IWorkspace* workspace)
{
    UiService& ui = Services.Ui;
    const WorkspaceOperationFacet operation = workspace != nullptr ? workspace->Operation() : WorkspaceOperationFacet{};
    (void)ui.SetValue(Screen, Ids.StationName, UiValue(operation.Name));
    (void)ui.SetValue(Screen, Ids.StationVariant, UiValue(operation.Variant));
    (void)ui.SetValue(Screen, Ids.StationIcon, UiValue(std::string(IconName(operation.Icon))));
    (void)ui.SetValue(Screen, Ids.StationHint, UiValue(operation.Hint));
    (void)ui.SetValue(Screen, Ids.Draft, UiValue(std::string(DraftName(operation.Draft))));
    std::vector<PropertySection> parameters;
    if (operation.Parameters != nullptr)
        parameters.push_back({ .Heading = {}, .Source = operation.Parameters });
    // The card is up while there is something on it beyond a hint: settings,
    // or drafted work to apply or cancel.
    const std::optional<std::size_t> rows = Sheets->Present(SheetId::Tool, parameters, false);
    if (!rows.has_value())
        return;
    ToolCardUp = *rows > 0 || operation.Draft != ToolDraft::None;
    (void)ui.SetValue(Screen, Ids.CardOpen, UiValue(ToolCardUp));
}

void ShellWindow::PublishCaption()
{
    // The window is dragged from the rail wherever nothing is mounted: the span
    // between its two clusters. Not while a popover is open, whose catch covers
    // the rail too.
    WindowFrameRegions regions;
    regions.ResizeBorder = static_cast<std::int32_t>(std::lround(4.0f * Services.Ui.GetSurfaceScale(SurfaceId)));
    const std::optional<UiElementBox>& rail = Rail;
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
    regions.CaptionEnabled = Menus->Showing() == RailMenus::Popover::None && !Prompt->IsShowing();
    Services.Window.SetFrameRegions(regions);
}
