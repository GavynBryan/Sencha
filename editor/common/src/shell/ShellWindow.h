#pragma once

#include "actions/ActionContext.h"
#include "shell/CommandPalette.h"
#include "shell/PromptDialog.h"
#include "shell/PropertySheet.h"
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
    // publish what it should show. `now` is presentation seconds, for notices;
    // `requestedScale` is the person's UI scale, 0 to follow this window's
    // display.
    void Update(double now, double requestedScale);

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
    enum class Popover : std::uint8_t { None, Hatch, Workspaces, Document, Tools, Category };

    void HandleActions();
    void Publish(double now);
    void PublishEntries();
    void PublishCaption();
    // The rail yields the pointer while the content holds it, so a drag
    // released over the rail still ends where it began.
    void YieldPointerToContent();
    void OpenPopover(Popover popover, std::string category = {});
    // A popover as a document or a script names it: "hatch", "tools",
    // "category:Grid", or anything else for none.
    void OpenNamedPopover(std::string_view name);
    [[nodiscard]] std::string PopoverName() const;
    void Choose(const std::string& key);

    [[nodiscard]] std::vector<UiRow> HatchEntries() const;
    [[nodiscard]] std::vector<UiRow> WorkspaceEntries() const;
    [[nodiscard]] std::vector<UiRow> CategoryEntries(std::string_view category) const;
    [[nodiscard]] std::vector<UiRow> ToolEntries();
    // The operation in hand, bottom left, and its card.
    void PublishStation(IWorkspace* workspace);
    // How the hands act and how the work is seen: the readouts beside the
    // stations, each opening its category's actions.
    void PublishReadouts(const IWorkspace* workspace);

    ShellWindowServices Services;
    UiSurfaceId SurfaceId;
    // Display-only, in kContentUiLayer: where workspaces draw their content.
    UiSurfaceId ContentSurfaceId;
    // Display-only, over the shell's own surface.
    UiSurfaceId OverlaySurfaceId;
    UiScreenHandle Screen;

    Popover Showing = Popover::None;
    std::string PopoverCategory;
    TransientSurfaces Transients;
    TransientSurfaces::Id PopoverTransient = 0;
    std::string ShownTitle;
    UiSurfaceInputPolicy InputPolicy = UiSurfaceInputPolicy::Full;
    // Escape belongs to the popover while one is open.
    ActionContext PopoverActions;
    ActionContextStack::Layer PopoverLayer;
    std::unique_ptr<CommandPalette> Palette;
    PropertySheet ToolSheet;
    // After the palette, so a question opens over it.
    std::unique_ptr<PromptDialog> Prompt;
    // What the shell itself performs in its window, whatever is showing.
    ActionContext WindowActions;
    ActionContextStack::Layer WindowLayer;
};
