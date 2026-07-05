#pragma once

#include <app/Game.h>

#include <memory>
#include <optional>
#include <string>

class MaterialEditorServices;

// The material editor's Game entry point: owns the MaterialEditorServices
// composition root and forwards each Game lifecycle hook to it.
class MaterialEditorApp : public Game
{
public:
    explicit MaterialEditorApp(std::optional<std::string> projectPath);
    ~MaterialEditorApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    std::optional<std::string> ProjectPath;
    std::unique_ptr<MaterialEditorServices> Services;
};
