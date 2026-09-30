#pragma once

#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceKind.h"

#include <graphics/PresentationId.h>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct FrameUpdateContext;

enum class WorkspaceAction
{
    Open,
    Close,
    Activate,
    // Into a window of its own, and back into the main one.
    Detach,
    Attach,
};

struct WorkspaceRequest
{
    WorkspaceAction Action = WorkspaceAction::Open;
    std::string Kind;
};

// The workspaces an application has open, at most one per kind, and the
// window each is placed in, which shows the one most recently activated there.
// Headless: a window is the presentation that shows it, and the application
// makes and closes those. The listeners let a window adopt a workspace's
// panels once it exists and let go of them before it is destroyed.
class WorkspaceHost
{
public:
    struct Entry
    {
        const WorkspaceKind* Kind = nullptr;
        std::unique_ptr<IWorkspace> Instance;
        PresentationId Window;
    };
    using Listener = std::function<void(const WorkspaceKind&, IWorkspace&)>;
    // Whether a workspace may close now. One that may not stays open; whoever
    // refused settles what stopped it and asks again.
    using CloseGuard = std::function<bool(const WorkspaceKind&, IWorkspace&)>;
    // Makes a window for a workspace being detached; null when it cannot.
    using WindowOpener = std::function<PresentationId(const WorkspaceKind&)>;
    using WindowListener = std::function<void(PresentationId)>;

    WorkspaceHost(std::vector<WorkspaceKind> kinds, bool hasProject, PresentationId mainWindow);
    ~WorkspaceHost();

    WorkspaceHost(const WorkspaceHost&) = delete;
    WorkspaceHost& operator=(const WorkspaceHost&) = delete;

    void SetOpenedListener(Listener listener) { OnOpened = std::move(listener); }
    void SetClosingListener(Listener listener) { OnClosing = std::move(listener); }
    void SetCloseGuard(CloseGuard guard) { MayClose = std::move(guard); }
    void SetWindowOpener(WindowOpener opener) { OpenWindow = std::move(opener); }
    // Runs after a workspace moves to another window, before it shows there.
    void SetPlacedListener(Listener listener) { OnPlaced = std::move(listener); }
    // A window other than the main one that no workspace is placed in any more.
    void SetWindowEmptiedListener(WindowListener listener) { OnWindowEmptied = std::move(listener); }

    [[nodiscard]] std::span<const WorkspaceKind> Kinds() const { return Table; }
    // Known, and not needing a project this application does not have.
    [[nodiscard]] bool IsOffered(std::string_view kind) const;

    // These change which workspaces exist, and a workspace adds and removes
    // render features as it comes and goes, so they must not run while the
    // renderer records a frame. Mid-frame callers Request instead.
    IWorkspace* Open(std::string_view kind);
    bool Close(std::string_view kind);
    bool Activate(std::string_view kind);
    // Moves a workspace to `window`, where it becomes the one showing.
    bool Place(std::string_view kind, PresentationId window);
    bool Detach(std::string_view kind);
    bool Attach(std::string_view kind) { return Place(kind, Main); }
    // Brings every workspace in `window` back to the main one, as closing it does.
    void Gather(PresentationId window);
    // Past the close guard: this is the way out, after the application's exit
    // has settled every document.
    void CloseAll();

    void Request(WorkspaceRequest request) { Pending.push_back(std::move(request)); }
    void ApplyRequests();

    // Applies what was requested since the last frame, then ticks every open
    // workspace.
    void Tick(FrameUpdateContext& ctx);

    // The one activated most recently, in whichever window.
    [[nodiscard]] IWorkspace* Active() const;
    [[nodiscard]] const WorkspaceKind* ActiveKind() const;
    // The one `window` shows.
    [[nodiscard]] IWorkspace* ActiveIn(PresentationId window) const;
    [[nodiscard]] const WorkspaceKind* ActiveKindIn(PresentationId window) const;
    [[nodiscard]] PresentationId MainWindow() const { return Main; }
    [[nodiscard]] PresentationId WindowOf(std::string_view kind) const;
    [[nodiscard]] IWorkspace* Find(std::string_view kind) const;
    // In the order they were opened, which is the order their tabs show.
    [[nodiscard]] std::span<const Entry> OpenWorkspaces() const { return Opened; }

private:
    [[nodiscard]] const WorkspaceKind* FindKind(std::string_view kind) const;
    [[nodiscard]] Entry* FindEntry(std::string_view kind);
    [[nodiscard]] Entry* FindEntry(const WorkspaceKind* kind);
    bool Destroy(std::string_view kind);
    // Shows what `window` now shows, or reports it empty.
    void SettleWindow(PresentationId window);

    std::vector<WorkspaceKind> Table;
    bool HasProject = false;
    PresentationId Main;
    std::vector<Entry> Opened;
    // Most recently active last. Each window shows the latest of its own, so
    // closing the one showing falls back to the one used there before it.
    std::vector<const WorkspaceKind*> RecentlyActive;
    std::vector<WorkspaceRequest> Pending;
    Listener OnOpened;
    Listener OnClosing;
    Listener OnPlaced;
    WindowListener OnWindowEmptied;
    WindowOpener OpenWindow;
    CloseGuard MayClose;
};
