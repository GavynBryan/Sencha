#pragma once

#include "actions/ActionContext.h"
#include "shell/CommandPalette.h"
#include "shell/AssetTabs.h"
#include "shell/PromptDialog.h"
#include "shell/SurfacePlacement.h"
#include "shell/TimelineDeck.h"
#include "input/InputEvent.h"
#include "presenters/PropertySheet.h"
#include "properties/PropertyEditSession.h"
#include "shell/ScrubGesture.h"
#include "shell/Shelf.h"
#include "shell/TransientSurfaces.h"
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
class NoticeQueue;
class ProblemList;
class PromptQueue;
class SdlWindow;
class UiService;
class IWorkspace;
class WorkspaceHost;
struct UiAction;
struct UiRow;

// The surface layer a window's content draws in, under the shell's own layer 0.
inline constexpr std::uint8_t kContentUiLayer = 1;

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
    PresentationId Presentation;
    // What the rail's close key does: leave the editor from the main window,
    // take a detached window away from any other.
    std::function<void()> CloseWindow;
    // The application's name, which the window's title leads with.
    std::string Title;
    ShelfSources Content = {};
};

// What the person has chosen about how every shell looks and moves.
struct ShellPreferences
{
    // 0 follows each window's display.
    double Scale = 0.0;
    // Transitions play; off, everything changes at once.
    bool Motion = true;
};

//=============================================================================
// ShellWindow
//
// One window's shell, in the current interaction grammar
// (docs/editor/interaction-grammar.md): the rail along the top -- the hatch,
// the workspace and document dials, the problems lamp, the transport and the
// window keys -- and the popovers its instruments open. It owns the window's
// one interactive authored surface.
//
// It presents facts and performs actions; it holds no editor state of its own
// beyond which popover is open. What a workspace is doing comes from its facets
// and the actions claimed in the window's stack, so this can be replaced by a
// different grammar without a workspace changing.
//=============================================================================
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
    // A number in a sheet is pressed or being dragged: the pointer and Escape
    // are its until let go, before the workspace sees them.
    [[nodiscard]] bool Scrubbing() const { return Scrub != ScrubPhase::Resting; }
    // Whether the shell took `event` for the number being dragged.
    bool TakeScrubInput(const InputEvent& event);

    // How much of the top of the window the rail covers, in window points:
    // where content under it has to start.
    [[nodiscard]] float RailHeight() const;

    [[nodiscard]] UiSurfaceId Surface() const { return SurfaceId; }
    // What a workspace placed in this window is handed.
    [[nodiscard]] WorkspaceWindow Binding() const;

    // Opens one of the rail's popovers by name ("hatch", "tools"), or
    // closes it with anything else: what a script or a test
    // drives instead of a click.
    void ShowPopover(std::string_view name);

private:
    // Category: a category's actions, named by PopoverCategory, from the
    // instrument whose id is "cat_" and the category.
    // Choices: what a sheet's row asks to be chosen -- a verb's choices, or an
    // asset row's candidates -- for the row ChoicesRow of the sheet
    // ChoicesSheet names, listed once as it opens.
    enum class Popover : std::uint8_t { None, Hatch, Tools, Category, Choices, Problems };

    void HandleActions();
    void Publish(double now);
    void PublishEntries();
    void PublishCaption();
    // The rail yields the pointer while the content holds it, so a drag
    // released over the rail still ends where it began.
    void YieldPointerToContent();
    void OpenPopover(Popover popover, std::string category = {});
    // What the row's verb asks to be chosen first.
    void OpenChoices(std::string sheet, std::size_t row, std::string verb, std::vector<std::string> choices,
                     std::vector<std::string> notes = {});
    // Which number of which row is being typed into, as the document names it.
    [[nodiscard]] static std::string TypingKey(std::string_view sheet, std::size_t row, std::int64_t element);
    // What was typed into a row's number, or one of a vector's, set as one
    // step; how many numbers the row has.
    std::size_t FinishTyping(PropertySheet& sheet, std::span<const PropertySection> sections, bool tool,
                             std::size_t index, std::int64_t element);
    // A sheet's verb done; one that takes something away says how to bring it back.
    void ActOnSheet(PropertySheet& sheet, std::span<const PropertySection> sections, std::size_t index,
                    std::string_view verb, std::string_view choice);
    // A popover as a document or a script names it: "hatch", "tools",
    // "category:Grid", or anything else for none.
    void OpenNamedPopover(std::string_view name);
    [[nodiscard]] std::string PopoverName() const;
    void Choose(const std::string& key);

    [[nodiscard]] std::vector<UiRow> HatchEntries() const;
    [[nodiscard]] std::vector<UiRow> WorkspaceEntries() const;
    [[nodiscard]] std::vector<UiRow> CategoryEntries(std::string_view category) const;
    // A tab per asset open in an editor this window shows; choosing one shows
    // its editor and the asset, and dragging one well below the rail moves its
    // editor into a window of its own.
    void PublishTabs();
    void HandleTabAction(const UiAction& action);
    [[nodiscard]] std::vector<UiRow> ToolEntries();
    // The operation in hand, bottom left, and its card.
    void PublishStation(IWorkspace* workspace);
    // How the hands act and how the work is seen: the readouts beside the
    // stations, each opening its category's actions.
    void PublishReadouts(const IWorkspace* workspace);
    // What the hands hold, bottom right: the primary resource and its stash.
    void PublishHolding(IWorkspace* workspace);
    // What is selected, bottom right above what is held: its card.
    void PublishSelection(IWorkspace* workspace);
    // A row of the tool card or the selection card edited, focused or let go.
    void HandleSheetAction(const UiAction& action);
    // The sources behind a sheet's rows, asked for again: the workspace's frame
    // may have moved on since they were presented.
    [[nodiscard]] std::vector<PropertySection> SheetSections(std::string_view sheet, IWorkspace& workspace);
    // The selection's rows as its view shows them: the card's settings, or in
    // the drawer everything.
    [[nodiscard]] std::vector<PropertySection> SelectionSections(const WorkspaceSelectionFacet& selection) const;
    void ShowDrawer();
    // The selection's title let go of: what it holds is the selection's new name.
    void RenameSelection();
    // While anything transient is open, Escape takes it away one at a time.
    void TakeEscape();
    void EscapeFront();
    // Asks the workspace a listed problem belongs to to show where it is.
    void RevealProblem(std::string_view index);
    // Opens the named explorer in place of any other; empty closes it.
    void ShowExplorer(std::string explorer);
    // The active workspace's explorers are opened by their actions, which this
    // window performs while that workspace is the one showing.
    void ClaimExplorers(const IWorkspace* workspace);
    void PublishExplorer(IWorkspace* workspace);
    // Where the work is, along the top: each part opens the workspace's
    // hierarchy explorer showing it.
    void PublishLocation(const IWorkspace* workspace);
    void RevealLocation(std::int64_t index);
    [[nodiscard]] std::string HierarchyAction(const IWorkspace& workspace,
                                              const WorkspaceLocationFacet& location) const;
    // The explorer and the selection, each where the person put it or where it
    // opened: the selection at its own place, the explorer in free space.
    void PlaceFloatingSurfaces();
    // A floating surface pressed (brought to the front), its header or corner
    // taken hold of, dragged, let go, or double-clicked back to where it opens.
    void HandleMoveAction(const UiAction& action);
    // A surface's border box as laid out, and where a moved surface must stay.
    [[nodiscard]] std::optional<Rect2d> MeasureBorder(std::string_view elementId) const;
    [[nodiscard]] Rect2d FloatingBounds() const;

    ShellWindowServices Services;
    UiSurfaceId SurfaceId;
    // Display-only, in kContentUiLayer: where workspaces draw their content.
    UiSurfaceId ContentSurfaceId;
    // Display-only, over the shell's own surface.
    UiSurfaceId OverlaySurfaceId;
    UiScreenHandle Screen;

    Popover Showing = Popover::None;
    std::string PopoverCategory;
    std::string ChoicesSheet;
    std::size_t ChoicesRow = 0;
    std::string ChoicesVerb;
    std::vector<std::string> ChoicesListed;
    std::vector<std::string> ChoicesNotes;
    // What has been typed to narrow the choices.
    std::string ChoicesFilter;
    TransientSurfaces Transients;
    TransientSurfaces::Id PopoverTransient = 0;
    std::string ShownTitle;
    UiSurfaceInputPolicy InputPolicy = UiSurfaceInputPolicy::Full;
    ActionContext EscapeActions;
    ActionContextStack::Layer EscapeLayer;
    std::unique_ptr<CommandPalette> Palette;
    PropertySheet ToolSheet;
    PropertySheet SelectionSheet;
    std::string ShownSelection;
    // The selection as a card, or turned into the drawer that shows all of it.
    enum class SelectionView : std::uint8_t { Card, Drawer };
    SelectionView SelectionShown = SelectionView::Card;
    bool TitleHeld = false;

    std::unique_ptr<Shelf> AssetShelf;
    TransientSurfaces::Id ShelfTransient = 0;
    // The workspace the shelf was last raised for because it had nothing open.
    std::string ShelvedFor;
    // The workspace's explorer the shelf is choosing for, while it is.
    std::string ChoosingFor;
    // Keeps the shelf's place among the window's open surfaces, and raises it
    // for a workspace showing nothing.
    void FollowShelf();

    // A number pressed in a sheet: dragged across it moves, released where it
    // was pressed it types. Every drag is one edit session, previewed live.
    enum class ScrubPhase : std::uint8_t { Resting, Pressed, Dragging };
    void PressNumber(std::string sheet, std::size_t row, std::int64_t element);
    void PreviewScrub(bool coarse);
    // What the drag writes now: the number, or its vector with the number in it.
    [[nodiscard]] std::string ScrubbedText() const;
    void EndScrub(bool commit);
    ScrubPhase Scrub = ScrubPhase::Resting;
    std::string ScrubSheet;
    std::size_t ScrubRow = 0;
    std::int64_t ScrubElement = -1;
    ScrubGesture ScrubMotion{ ScrubScale{} };
    PropertyEditSession ScrubSession;
    // A number its source names no key for is written by setting its vector
    // whole, the others as they began.
    enum class ScrubTarget : std::uint8_t { Number, WholeVector };
    ScrubTarget ScrubWrites = ScrubTarget::Number;
    // Where the number began, or that it stands for several and moves each by
    // the amount.
    double ScrubStart = 0.0;
    bool ScrubMixed = false;
    std::vector<std::string> ScrubParts;
    double ScrubAmount = 0.0;

    // The number being typed into instead of dragged, or empty.
    std::string SheetTyping;
    TransientSurfaces::Id DrawerTransient = 0;
    std::string ExplorerShown;
    std::string ExplorerFilter;
    std::size_t ExplorerFirst = 0;
    TransientSurfaces::Id ExplorerTransient = 0;
    std::vector<std::string> ClaimedExplorers;
    std::vector<AssetTab> ShownTabs;
    // Which shape of each floating surface is showing ("" for none), and the
    // shape last placed, which is let go when another shows instead.
    std::string ExplorerKey;
    std::string SelectionKey;
    std::string PlacedExplorerKey;
    std::string PlacedSelectionKey;
    bool ToolCardUp = false;
    SurfaceLayout Layout;
    // Which floating surface is drawn over the other where they overlap: the
    // one last opened, pressed or dragged.
    std::string FrontSurface;
    // The row whose arrow or mark was pressed in this drain.
    std::string HandledInRow;
    // After the palette, so a question opens over it.
    std::unique_ptr<PromptDialog> Prompt;
    std::unique_ptr<TimelineDeck> Deck;
    // What the shell itself performs in its window, whatever is showing.
    ActionContext WindowActions;
    ActionContextStack::Layer WindowLayer;
};
