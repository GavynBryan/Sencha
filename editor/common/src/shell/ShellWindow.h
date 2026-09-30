#pragma once

#include "actions/ActionContext.h"
#include "shell/CommandPalette.h"
#include "shell/PromptDialog.h"
#include "shell/TransientSurfaces.h"

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
class WorkspaceHost;
struct UiRow;

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
    // `scale` is the display scale the editor resolved.
    void Update(double now, float scale);

    // How much of the top of the window the rail covers, in window points:
    // where content under it has to start.
    [[nodiscard]] float RailHeight() const;

    [[nodiscard]] UiSurfaceId Surface() const { return SurfaceId; }

    // Opens one of the rail's popovers by name ("hatch", "workspaces",
    // "document"), or closes it with anything else: what a script or a test
    // drives instead of a click.
    void ShowPopover(std::string_view name);

private:
    enum class Popover : std::uint8_t { None, Hatch, Workspaces, Document };

    void HandleActions();
    void Publish(double now);
    void PublishEntries();
    void PublishCaption();
    void OpenPopover(Popover popover);
    void Choose(const std::string& key);

    [[nodiscard]] std::vector<UiRow> HatchEntries() const;
    [[nodiscard]] std::vector<UiRow> WorkspaceEntries() const;
    [[nodiscard]] std::vector<UiRow> DocumentEntries() const;

    ShellWindowServices Services;
    UiSurfaceId SurfaceId;
    UiScreenHandle Screen;

    Popover Showing = Popover::None;
    TransientSurfaces Transients;
    TransientSurfaces::Id PopoverTransient = 0;
    std::string ShownTitle;
    // Escape belongs to the popover while one is open.
    ActionContext PopoverActions;
    ActionContextStack::Layer PopoverLayer;
    std::unique_ptr<CommandPalette> Palette;
    // After the palette, so a question opens over it.
    std::unique_ptr<PromptDialog> Prompt;
    // What the shell itself performs in its window, whatever is showing.
    ActionContext WindowActions;
    ActionContextStack::Layer WindowLayer;
};
