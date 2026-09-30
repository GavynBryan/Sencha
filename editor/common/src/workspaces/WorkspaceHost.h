#pragma once

#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceKind.h"

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
};

struct WorkspaceRequest
{
    WorkspaceAction Action = WorkspaceAction::Open;
    std::string Kind;
};

// The workspaces an application has open: at most one per kind, one active.
// Headless. The listeners let a window adopt a workspace's panels once it
// exists and let go of them before it is destroyed.
class WorkspaceHost
{
public:
    struct Entry
    {
        const WorkspaceKind* Kind = nullptr;
        std::unique_ptr<IWorkspace> Instance;
    };
    using Listener = std::function<void(const WorkspaceKind&, IWorkspace&)>;
    // Whether a workspace may close now. One that may not stays open; whoever
    // refused settles what stopped it and asks again.
    using CloseGuard = std::function<bool(const WorkspaceKind&, IWorkspace&)>;

    WorkspaceHost(std::vector<WorkspaceKind> kinds, bool hasProject);
    ~WorkspaceHost();

    WorkspaceHost(const WorkspaceHost&) = delete;
    WorkspaceHost& operator=(const WorkspaceHost&) = delete;

    void SetOpenedListener(Listener listener) { OnOpened = std::move(listener); }
    void SetClosingListener(Listener listener) { OnClosing = std::move(listener); }
    void SetCloseGuard(CloseGuard guard) { MayClose = std::move(guard); }

    [[nodiscard]] std::span<const WorkspaceKind> Kinds() const { return Table; }
    // Known, and not needing a project this application does not have.
    [[nodiscard]] bool IsOffered(std::string_view kind) const;

    // These change which workspaces exist, and a workspace adds and removes
    // render features as it comes and goes, so they must not run while the
    // renderer records a frame. Mid-frame callers Request instead.
    IWorkspace* Open(std::string_view kind);
    bool Close(std::string_view kind);
    bool Activate(std::string_view kind);
    // Past the close guard: this is the way out, after the application's exit
    // has settled every document.
    void CloseAll();

    void Request(WorkspaceRequest request) { Pending.push_back(std::move(request)); }
    void ApplyRequests();

    // Applies what was requested since the last frame, then ticks every open
    // workspace.
    void Tick(FrameUpdateContext& ctx);

    [[nodiscard]] IWorkspace* Active() const;
    [[nodiscard]] const WorkspaceKind* ActiveKind() const { return ActiveEntryKind; }
    [[nodiscard]] IWorkspace* Find(std::string_view kind) const;
    // In the order they were opened, which is the order their tabs show.
    [[nodiscard]] std::span<const Entry> OpenWorkspaces() const { return Opened; }

private:
    [[nodiscard]] const WorkspaceKind* FindKind(std::string_view kind) const;
    bool Destroy(std::string_view kind);

    std::vector<WorkspaceKind> Table;
    bool HasProject = false;
    std::vector<Entry> Opened;
    // Most recently active last, so closing the active one falls back to the
    // one used before it.
    std::vector<const WorkspaceKind*> RecentlyActive;
    const WorkspaceKind* ActiveEntryKind = nullptr;
    std::vector<WorkspaceRequest> Pending;
    Listener OnOpened;
    Listener OnClosing;
    CloseGuard MayClose;
};
