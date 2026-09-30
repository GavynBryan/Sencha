#pragma once

#include <app/Game.h>

#include <memory>
#include <optional>
#include <string>

class EditorServices;
class ProjectSession;

// The editor's Game entry point: it owns the project session and the level
// editor built over it, and forwards each Game lifecycle hook. Glue; the
// wiring lives in the objects it owns.
class EditorApp : public Game
{
public:
    // Defined out of line so the .cpp sees the complete EditorServices the
    // unique_ptr member forward-declares. projectPath is the resolved
    // .senchaproj path (ResolveProjectPath), if any.
    explicit EditorApp(std::optional<std::string> projectPath);
    ~EditorApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    std::optional<std::string> ProjectPath;
    // Declared before the editor so it is destroyed after it: the module stays
    // mapped while any document holds code it compiled.
    std::unique_ptr<ProjectSession> Session;
    std::unique_ptr<EditorServices> Services;
};
