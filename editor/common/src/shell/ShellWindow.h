#pragma once

#include "actions/ActionContext.h"
#include "input/InputEvent.h"
#include "input/InputRouter.h"
#include "shell/AssetTabs.h"
#include "shell/CommandPalette.h"
#include "shell/ExplorerPanel.h"
#include "shell/PromptDialog.h"
#include "shell/RailMenus.h"
#include "shell/SheetPanels.h"
#include "shell/Shelf.h"
#include "shell/SurfacePlacement.h"
#include "shell/TimelineDeck.h"
#include "shell/TransientSurfaces.h"
#include "tools/RadialMenuSession.h"
#include "workspaces/WorkspaceWindow.h"

#include <graphics/PresentationId.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ActionCatalog;
class ColorPicker;
class NoticeQueue;
class ProblemList;
class PromptQueue;
class RadialMenuScreen;
class PlatformEventRouter;
class ScreenModel;
class SdlWindow;
class UiService;
class IWorkspace;
class WorkspaceHost;
struct UiAction;

// The surface layer a window's content draws in, under the shell's own layer 0.
inline constexpr std::uint8_t kContentUiLayer = 1;

// What a window is for: the workspaces placed in it, or the shelf alone.
enum class ShellRole : std::uint8_t
{
    Workspaces,
    Shelf,
};

// What a window's shell presents and acts through. The action stack is the
// window's own: everything the rail does goes through it, so a key on the rail
// and a chord on the keyboard perform the same thing.
struct ShellWindowServices
{
    UiService& Ui;
    WorkspaceHost& Workspaces;
    ActionCatalog& Catalog;
    ActionContextStack& Actions;
    ProblemList& Problems;
    NoticeQueue& Notices;
    PromptQueue& Prompts;
    SdlWindow& Window;
    // Hands the shell a gesture its document began, as a scrub does.
    PlatformEventRouter& Pointer;
    PresentationId Presentation;
    // What the rail's close key does: leave the editor from the main window,
    // take a detached window away from any other.
    std::function<void()> CloseWindow;
    // The application's name, which the window's title leads with.
    std::string Title;
    ShelfSources Content = {};
    ShellRole Role = ShellRole::Workspaces;
};

// What the person has chosen about how every shell looks and moves.
struct ShellPreferences
{
    // 0 follows each window's display.
    double Scale = 0.0;
    // Transitions play; off, everything changes at once.
    bool Motion = true;
};

// One window's shell, in the current interaction grammar
// (docs/editor/interaction-grammar.md), composed of the parts that each own a
// piece of it: the rail's popovers, the sheets, the explorer, the shelf, the
// timeline deck, the palette and prompts. What a workspace is doing comes from
// its facets and the actions claimed in the window's stack.
class ShellWindow
{
public:
    explicit ShellWindow(ShellWindowServices services);
    ~ShellWindow();

    ShellWindow(const ShellWindow&) = delete;
    ShellWindow& operator=(const ShellWindow&) = delete;

    // Per frame, before the UI updates: act on what the rail asked for, then
    // publish what it should show. `now` is presentation seconds, for notices.
    void Update(double now, const ShellPreferences& preferences);
    // The pointer and the keys are the shell's, before the workspace sees
    // them: a number in a sheet pressed or being dragged, or a wheel up.
    [[nodiscard]] bool HoldsInput() const { return Sheets->HoldsInput() || WheelUp(); }
    // Whether the shell took `event` for what holds it.
    bool TakeHeldInput(const InputEvent& event);
    // Files from outside the project dropped on the window, imported onto the shelf.
    void Import(std::span<const std::filesystem::path> files) { AssetShelf->Import(files); }

    // How much of the top of the window the rail covers, in window points:
    // where content under it has to start.
    [[nodiscard]] float RailHeight() const;

    [[nodiscard]] UiSurfaceId Surface() const { return SurfaceId; }
    // What a workspace placed in this window is handed.
    [[nodiscard]] WorkspaceWindow Binding() const;

    // Opens one of the rail's popovers by name, or closes it: what a script
    // drives instead of a click.
    void ShowPopover(std::string_view name) { Menus->ShowNamed(name); }

private:
    // The ids of what the shell itself presents and is asked for.
    struct ModelIds
    {
        UiModelPropertyId ProblemCount, ProblemErrors, TransportState, TransportText, CookEnabled, PlayEnabled,
            StopEnabled, StationName, StationVariant, StationIcon, StationHint, Draft, CardOpen, Element, GizmoMode,
            GizmoSpace, Grid, Snap, Pivot, Projection, ViewCount, HoldingRole, HoldingLabel, HoldingImage,
            HoldingExplorer, Motion, SelectionTitle, SelectionStaged, Drawer, SelectionSubtitle, Sheet, ExplorerLeft,
            ExplorerTop, ExplorerWidth, ExplorerHeight, SelectionLeft, SelectionTop, SelectionWidth, SelectionHeight,
            Front, HierarchyChord, SelectionShown, MapAction, SelectionNamed, InstrumentsLift;
        UiModelRowsId Notices, Stash, SelectionVerbs, Crumbs, Tabs;
    };
    struct OwnAction
    {
        UiActionId Id;
        std::function<void(const UiAction&)> Handle;
    };
    void Declare(ScreenModel& model);
    void Own(ScreenModel& model, std::string name, std::function<void(const UiAction&)> handle);

    void HandleActions();
    void OnInvoke(const UiAction& action);
    void OnWindowKey(const UiAction& action);
    void OnDrawer(const UiAction& action);
    void OnTitleHold(const UiAction& action);
    void OnTitleRelease(const UiAction& action);
    // A floating surface pressed (brought to the front), its header or corner
    // taken hold of, dragged, let go, or double-clicked back to where it opens.
    enum class SurfaceMove : std::uint8_t { Raise, Grab, Drag, Drop, Reset };
    void MoveSurface(const UiAction& action, SurfaceMove move);
    void OnTab(const UiAction& action);
    void OnTabDrop(const UiAction& action);
    void OnCrumb(const UiAction& action);

    void Publish(double now);
    void PublishCaption();
    void PublishTabs();
    // The operation in hand, bottom left, and its card.
    void PublishStation(IWorkspace* workspace);
    // How the hands act and how the work is seen: the readouts beside the
    // stations, each opening its category's actions.
    void PublishReadouts(const IWorkspace* workspace);
    // What the hands hold, bottom right: the primary resource and its stash.
    void PublishHolding(IWorkspace* workspace);
    // What is selected, bottom right above what is held: its card.
    void PublishSelection(IWorkspace* workspace);
    // Where the work is, along the top: each part opens the workspace's
    // hierarchy explorer showing it.
    void PublishLocation(const IWorkspace* workspace);

    // The wheels the shelf opens -- a tile's actions, what can be made new --
    // each chosen from by a click, each on a router of its own for the pointer
    // capture it takes.
    struct ClickWheel
    {
        std::unique_ptr<RadialMenuSession> Session;
        InputRouter Input;
    };
    [[nodiscard]] ClickWheel* OpenWheel();
    [[nodiscard]] const ClickWheel* OpenWheel() const { return const_cast<ShellWindow*>(this)->OpenWheel(); }
    [[nodiscard]] bool WheelUp() const { return OpenWheel() != nullptr; }
    void MakeClickWheel(ClickWheel& wheel, IRadialMenuModel& model);
    [[nodiscard]] RadialMenu::Frame WheelFrame() const;
    void YieldPointerToWheel();

    void OpenColor(SheetPoint at);
    // The sources behind a sheet's rows, asked for again: the workspace's frame
    // may have moved on since they were presented.
    [[nodiscard]] std::vector<PropertySection> SheetSections(SheetId sheet);
    // The selection's rows as its view shows them: the card's settings, or in
    // the drawer everything.
    [[nodiscard]] std::vector<PropertySection> SelectionSections(const WorkspaceSelectionFacet& selection) const;
    void ShowDrawer();
    // While anything transient is open, Escape takes it away one at a time.
    void TakeEscape();
    void EscapeFront();
    // Asks the workspace a listed problem belongs to to show where it is.
    void RevealProblem(std::string_view index);
    void RevealLocation(std::int64_t index);
    [[nodiscard]] std::string HierarchyAction(const IWorkspace& workspace,
                                              const WorkspaceLocationFacet& location) const;
    // What the window shows moved on -- another workspace, another selection:
    // whatever was held for what it showed before ends unapplied.
    void FollowWorkspace(const IWorkspace* workspace);
    void OfferWindowActions();
    // The explorer and the selection, each where the person put it or where it
    // opened: the selection at its own place, the explorer in free space.
    void PlaceFloatingSurfaces();
    // A surface's border box as laid out, and where a moved surface must stay.
    [[nodiscard]] std::optional<Rect2d> MeasureBorder(std::string_view elementId) const;
    [[nodiscard]] Rect2d FloatingBounds() const;
    // Keeps the shelf's place among the window's open surfaces, and raises it
    // for a workspace showing nothing.
    void FollowShelf();

    ShellWindowServices Services;
    UiSurfaceId SurfaceId;
    // Display-only, in kContentUiLayer: where workspaces draw their content.
    UiSurfaceId ContentSurfaceId;
    // Display-only, over the shell's own surface.
    UiSurfaceId OverlaySurfaceId;
    UiScreenHandle Screen;
    ModelIds Ids;
    std::vector<OwnAction> OwnActions;

    TransientSurfaces Transients;
    std::string ShownTitle;
    UiSurfaceInputPolicy InputPolicy = UiSurfaceInputPolicy::Full;
    ActionContext EscapeActions;
    ActionContextStack::Layer EscapeLayer;
    std::unique_ptr<CommandPalette> Palette;
    std::unique_ptr<ColorPicker> ColorPick;
    std::unique_ptr<Shelf> AssetShelf;
    std::unique_ptr<SheetPanels> Sheets;
    std::unique_ptr<ExplorerPanel> Explorer;
    std::unique_ptr<RailMenus> Menus;

    // The workspace and selection last shown, to notice either moving on.
    const IWorkspace* ShownWorkspace = nullptr;
    std::uint64_t ShownRevision = 0;
    std::string ShownSelection;
    // The selection as a card, or turned into the drawer that shows all of it.
    enum class SelectionView : std::uint8_t { Card, Drawer };
    SelectionView SelectionShown = SelectionView::Card;
    TransientSurfaces::Id DrawerTransient = 0;
    // The selection's title being typed into: the name it renames, taken as the
    // field took focus, so a rename lands on what was named.
    enum class TitleEdit : std::uint8_t { Resting, Held };
    TitleEdit Title = TitleEdit::Resting;
    PropertyRef Renaming;

    ClickWheel FileWheel;
    ClickWheel NewWheel;
    std::unique_ptr<RadialMenuScreen> WheelScreen;
    TransientSurfaces::Id ShelfTransient = 0;
    // The workspace the shelf was last raised for because it had nothing open.
    std::string ShelvedFor;

    std::vector<AssetTab> ShownTabs;
    // Which shape of the selection's surface is showing ("" for none), and the
    // shapes last placed, let go when another shows instead.
    std::string SelectionKey;
    std::string PlacedExplorerKey;
    std::string PlacedSelectionKey;
    bool ToolCardUp = false;
    SurfaceLayout Layout;
    // Which floating surface is drawn over the other where they overlap: the
    // one last opened, pressed or dragged.
    std::string FrontSurface;
    // After the palette, so a question opens over it.
    std::unique_ptr<PromptDialog> Prompt;
    std::unique_ptr<TimelineDeck> Deck;
    // What the shell itself performs in its window, whatever is showing.
    ActionContext WindowActions;
    ActionContextStack::Layer WindowLayer;
};
