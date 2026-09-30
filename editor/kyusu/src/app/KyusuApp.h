#pragma once

#include <app/Game.h>

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

    std::optional<std::string> ProjectPath;
    // Declared before the workspaces so it is destroyed after them: the module
    // stays mapped while any document holds code it compiled.
    std::unique_ptr<ProjectSession> Session;
    std::unique_ptr<WorkspaceHost> Workspaces;
    // Owned by the renderer.
    EditorUiFeature* Ui = nullptr;
};
