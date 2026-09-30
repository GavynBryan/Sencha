#pragma once

#include "actions/ActionContext.h"
#include "shell/CommandPalette.h"
#include "shell/PromptDialog.h"
#include "presenters/PropertySheet.h"
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

// The surface layer a window's content draws in: under the window's legacy
// panels, which draw between it and the shell's own layer 0.
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

    // How much of the top of the window the rail covers, in window points:
    // where content under it has to start.
    [[nodiscard]] float RailHeight() const;
    // How much of the bottom of the window the stations take, in window
    // points: where docked content has to end while the stations sit in a
    // band of their own.
    [[nodiscard]] float StationBand() const;

    [[nodiscard]] UiSurfaceId Surface() const { return SurfaceId; }
    // What a workspace placed in this window is handed.
    [[nodiscard]] WorkspaceWindow Binding() const;

    // Opens one of the rail's popovers by name ("hatch", "workspaces",
    // "document"), or closes it with anything else: what a script or a test
    // drives instead of a click.
    void ShowPopover(std::string_view name);

private:
    // Category: a category's actions, named by PopoverCategory, from the
    // instrument whose id is "cat_" and the category.
    // Choices: what a sheet's row asks to be chosen -- a verb's choices, or an
    // asset row's candidates -- for the row ChoicesRow of the sheet
    // ChoicesSheet names, listed once as it opens.
    enum class Popover : std::uint8_t { None, Hatch, Workspaces, Document, Tools, Category, Choices, Problems };
    enum class ChoiceFor : std::uint8_t { Verb, Value };

    void HandleActions();
    void Publish(double now);
    void PublishEntries();
    void PublishCaption();
    // The rail yields the pointer while the content holds it, so a drag
    // released over the rail still ends where it began.
    void YieldPointerToContent();
    void OpenPopover(Popover popover, std::string category = {});
    void OpenChoices(std::string sheet, std::size_t row, ChoiceFor kind, std::vector<std::string> choices);
    // A popover as a document or a script names it: "hatch", "tools",
    // "category:Grid", or anything else for none.
    void OpenNamedPopover(std::string_view name);
    [[nodiscard]] std::string PopoverName() const;
    void Choose(const std::string& key);

    [[nodiscard]] std::vector<UiRow> HatchEntries() const;
    [[nodiscard]] std::vector<UiRow> WorkspaceEntries() const;
    [[nodiscard]] std::vector<UiRow> CategoryEntries(std::string_view category) const;
    // The workspace's open documents, then its File actions.
    [[nodiscard]] std::vector<UiRow> DocumentEntries() const;
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
    // the drawer everything, narrowed to one component when one was chosen.
    [[nodiscard]] std::vector<PropertySection> SelectionSections(const WorkspaceSelectionFacet& selection) const;
    // Opens the selection's drawer, or turns the open one to `focus` (a
    // top-level property's key, or empty for all of it).
    void ShowDrawer(std::string focus);
    // While anything transient is open, Escape takes it away one at a time.
    void TakeEscape();
    // Asks the workspace a listed problem belongs to to show where it is.
    void RevealProblem(std::string_view index);
    // Opens the named explorer in place of any other; empty closes it.
    void ShowExplorer(std::string explorer);
    // The active workspace's explorers are opened by their actions, which this
    // window performs while that workspace is the one showing.
    void ClaimExplorers(const IWorkspace* workspace);
    void PublishExplorer(IWorkspace* workspace);

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
    ChoiceFor ChoicesFor = ChoiceFor::Verb;
    std::vector<std::string> ChoicesListed;
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
    std::string DrawerFocus;
    TransientSurfaces::Id DrawerTransient = 0;
    std::string ExplorerShown;
    std::string ExplorerFilter;
    std::size_t ExplorerFirst = 0;
    TransientSurfaces::Id ExplorerTransient = 0;
    std::vector<std::string> ClaimedExplorers;
    // The row whose arrow or mark was pressed in this drain.
    std::string HandledInRow;
    // After the palette, so a question opens over it.
    std::unique_ptr<PromptDialog> Prompt;
    // What the shell itself performs in its window, whatever is showing.
    ActionContext WindowActions;
    ActionContextStack::Layer WindowLayer;
};
