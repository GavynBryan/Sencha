#pragma once

#include <app/Game.h>

#include "input/ShortcutRegistry.h"

#include <memory>
#include <optional>
#include <string>

class EditorUiFeature;
class ProjectSession;
class WorkspaceHost;

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
    // The keys every workspace shares, for one that does not bind its own:
    // undo, redo, save and save all. Rebindable from keybinds.json.
    void BuildShortcuts();
    // The active workspace's staged edit, then the journal's newest step.
    void Undo();
    [[nodiscard]] bool CanUndo() const;
    // Whether this session starts in a level rather than choosing a project.
    [[nodiscard]] bool OpensLevel() const;

    std::optional<std::string> ProjectPath;
    // Declared before the workspaces so it is destroyed after them: the module
    // stays mapped while any document holds code it compiled.
    std::unique_ptr<ProjectSession> Session;
    std::unique_ptr<WorkspaceHost> Workspaces;
    // Owned by the renderer.
    EditorUiFeature* Ui = nullptr;
    // A workspace whose close is held until its changed documents are saved
    // or discarded.
    std::string HeldClose;
    std::string SettleError;
    ShortcutRegistry Shortcuts;
    // Until the first frame nothing records, so the startup script's workspace
    // commands apply at once and the commands after them find what they open.
    bool FramesStarted = false;
};
