#pragma once

#include <app/Game.h>

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "app/SynthesizedInput.h"
#include "documents/DocumentSourceSet.h"
#include "notices/NoticeQueue.h"
#include "operations/OperationSocket.h"
#include "operations/EditorOperations.h"
#include "problems/AssetLoadProblems.h"
#include "problems/ProblemList.h"
#include "prompts/DocumentPrompts.h"
#include "prompts/PromptQueue.h"
#include "shell/ShellWindow.h"
#include "shell/ThemeSheet.h"

#include <graphics/PresentationId.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SdlWindow;
class UiRenderFeature;
class IRenderFeature;
class IWorkspace;
class ProjectSession;
class WorkspaceHost;
struct WorkspaceKind;

// Kyusu's Game entry point: the project session, the windows' shells and the
// workspaces shown in them, built in that order and torn down in reverse.
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
    // Exit held while documents have changes, and what follows every save;
    // the journal bringing forward the workspace whose document it steps; and
    // a workspace with changes asking before it closes.
    void InstallDocumentActions();
    void AskBeforeClosing(const WorkspaceKind& kind, std::vector<DocumentRef> changed);
    // The operations every workspace shares -- undo, redo, save all --
    // and the user's keymap. Every action defined from then on is also a
    // console command of the same id.
    void DefineApplicationActions();
    // Puts each window's active workspace's actions in play there, and takes a
    // workspace's out once it is no longer the one in use.
    struct WindowActions;
    void SyncActionLayers();
    // The theme, then each window's shell: act on and publish its rail.
    void UpdateShells();
    // Opens or closes the operation socket as its cvar says, and answers it.
    void ServeOperations();
    [[nodiscard]] std::unique_ptr<ShellWindow> MakeShell(SdlWindow& window, PresentationId presentation,
                                                         WindowActions& actions, std::function<void()> close,
                                                         ShellRole role = ShellRole::Workspaces);
    [[nodiscard]] WindowActions& ActionsFor(PresentationId window);
    // The active workspace's staged edit, then the journal's newest step.
    void Undo();
    [[nodiscard]] bool CanUndo() const;
    // Whether this session starts in a level rather than choosing a project.
    [[nodiscard]] bool OpensLevel() const;
    // A window for a detached workspace and the shell drawn in it; null when
    // either cannot be made.
    PresentationId OpenDetachedWindow(const WorkspaceKind& kind);
    // A window of its own beside the main one, its shell in `role`; null when
    // it cannot be made.
    PresentationId OpenWindow(std::string title, ShellRole role);
    void CloseDetachedWindow(PresentationId window);
    // The window an event names; the primary for one naming none.
    [[nodiscard]] PresentationId WindowOf(std::uint32_t windowId) const;
    [[nodiscard]] ShellWindow* WindowShellFor(PresentationId window) const;
    // Where the open project's content is on disk; none without a project.
    [[nodiscard]] std::vector<std::filesystem::path> ContentRoots() const;
    // What opening the project could not do to give every file its own id.
    void PublishAdoptionProblems();
    // Content mounted beside the project's: the engine's and the editor's.
    [[nodiscard]] std::vector<std::filesystem::path> OtherContentRoots() const;
    // Windows asked to close mid-frame, closed at the next frame boundary.
    void CloseRequestedWindows();

    std::optional<std::string> ProjectPath;
    // Before everything that claims actions, so every window's stack and the
    // contexts in it are gone before the catalog they resolve against.
    ActionCatalog Catalog;
    // Every editing client's operations; before the workspaces that register
    // into it, so it outlives them.
    EditorOperations Operations;
    ActionContext ApplicationActions;
    ProblemList Problems;
    AssetLoadProblems LoadProblems{ Problems };
    NoticeQueue Notices;
    // The operations served on a local socket while editor.operations.socket is on.
    OperationSocket OperationServer{ Operations };
    // Refused holds a failed open until the cvar is turned off, so it is not
    // retried every frame.
    enum class Serving : std::uint8_t
    {
        Off,
        On,
        Refused,
    };
    Serving OperationServing = Serving::Off;
    // Tells clients of the operations when a document is saved.
    std::shared_ptr<DocumentSourceSet::ReportObserver::Fn> SaveEvents;
    // Before everything that asks, so every asker withdraws into a live queue.
    PromptQueue Prompts;
    struct WindowActions
    {
        ActionContextStack Stack;
        ActionContextStack::Layer Application;
        ActionContextStack::Layer Workspace;
        const IWorkspace* Layered = nullptr;
    };
    std::unique_ptr<WindowActions> MainWindowActions;
    // After the window's actions, so it lets go of its layer in them first.
    std::unique_ptr<ShellWindow> MainWindowShell;
    // Declared before the workspaces so it is destroyed after them: the module
    // stays mapped while any document holds code it compiled.
    std::unique_ptr<ProjectSession> Session;
    // The primary window's content UI pass borrows the engine's run-scoped
    // UI service and textures; withdraw it before those resources go away.
    IRenderFeature* MainContentUiFeature = nullptr;
    // Over the session's journal, so gone before it.
    std::unique_ptr<DocumentExitHold> ExitHold;
    std::unique_ptr<DocumentSaveFollowUp> SaveFollowUp;
    PromptQueue::Asker CloseAsker;
    std::unique_ptr<WorkspaceHost> Workspaces;
    std::unique_ptr<ThemeSheet> Theme;
    struct DetachedWindow
    {
        PresentationId Presentation;
        std::uint32_t WindowId = 0;
        std::unique_ptr<WindowActions> Actions;
        std::unique_ptr<ShellWindow> WindowShell;
        // Owned by the renderer: the window's shell, and its content under it.
        UiRenderFeature* Ui = nullptr;
        UiRenderFeature* ContentUi = nullptr;
    };
    std::vector<DetachedWindow> Detached;
    std::vector<PresentationId> WindowsToClose;
    // The shelf's window of its own, while it is open.
    PresentationId ShelfWindow;
    SynthesizedInput Input;
    // Until the first frame nothing records, so the startup script's workspace
    // commands apply at once and the commands after them find what they open.
    bool FramesStarted = false;
};
