#pragma once

#include <app/Game.h>

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"

#include <graphics/PresentationId.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class EditorUiFeature;
class IWorkspace;
class ProjectSession;
class WorkspaceHost;
struct WorkspaceKind;

// Kyusu's Game entry point: the project session, the window's UI and the
// workspaces drawn in it, built in that order and torn down in reverse.
class KyusuApp : public Game
{
public:
    // projectPath is the resolved .senchaproj path (ResolveProjectPath), if any.
    explicit KyusuApp(std::optional<std::string> projectPath);
    ~KyusuApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    void RegisterWorkspaceCommands();
    // Undo, redo, save all and the exit prompt over the session's journal;
    // the journal bringing forward the workspace whose document it steps; and
    // a workspace with changes asking before it closes.
    void InstallDocumentActions();
    void DrawClosePrompt();
    // The operations every workspace shares -- undo, redo, save, save all --
    // and the user's keymap. Every action defined from then on is also a
    // console command of the same id.
    void DefineApplicationActions();
    // Puts each window's active workspace's actions in play there, and takes a
    // workspace's out once it is no longer the one in use.
    void SyncActionLayers();
    struct WindowActions;
    [[nodiscard]] WindowActions& ActionsFor(PresentationId window);
    // The active workspace's staged edit, then the journal's newest step.
    void Undo();
    [[nodiscard]] bool CanUndo() const;
    // Whether this session starts in a level rather than choosing a project.
    [[nodiscard]] bool OpensLevel() const;
    // A window for a detached workspace and the shell drawn in it; null when
    // either cannot be made.
    PresentationId OpenDetachedWindow(const WorkspaceKind& kind);
    void CloseDetachedWindow(PresentationId window);
    // The shell of the window an event names; the primary's for one naming none.
    [[nodiscard]] EditorUiFeature* ShellForWindowId(std::uint32_t windowId) const;
    [[nodiscard]] EditorUiFeature* ShellFor(PresentationId window) const;
    // Windows asked to close mid-frame, closed at the next frame boundary.
    void CloseRequestedWindows();

    std::optional<std::string> ProjectPath;
    // Before everything that claims actions, so every window's stack and the
    // contexts in it are gone before the catalog they resolve against.
    ActionCatalog Catalog;
    ActionContext ApplicationActions;
    struct WindowActions
    {
        ActionContextStack Stack;
        ActionContextStack::Layer Application;
        ActionContextStack::Layer Workspace;
        const IWorkspace* Layered = nullptr;
    };
    std::unique_ptr<WindowActions> MainWindowActions;
    // Declared before the workspaces so it is destroyed after them: the module
    // stays mapped while any document holds code it compiled.
    std::unique_ptr<ProjectSession> Session;
    std::unique_ptr<WorkspaceHost> Workspaces;
    // Owned by the renderer.
    EditorUiFeature* Ui = nullptr;
    struct DetachedWindow
    {
        PresentationId Presentation;
        std::uint32_t WindowId = 0;
        // Owned by the renderer.
        EditorUiFeature* Shell = nullptr;
        std::unique_ptr<WindowActions> Actions;
    };
    std::vector<DetachedWindow> Detached;
    std::vector<PresentationId> WindowsToClose;
    // A workspace whose close is held until its changed documents are saved
    // or discarded.
    std::string HeldClose;
    std::string SettleError;
    // Until the first frame nothing records, so the startup script's workspace
    // commands apply at once and the commands after them find what they open.
    bool FramesStarted = false;
};
