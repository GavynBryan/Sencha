#pragma once

#include <app/Game.h>

#include <memory>
#include <optional>
#include <string>

class ShojiServices;

// The previewer's Game entry point: owns the composition root and forwards
// each lifecycle hook to it. The same two-file shape as every editor.
class ShojiApp : public Game
{
public:
    ShojiApp(std::optional<std::string> projectPath, std::optional<std::string> document);
    ~ShojiApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    std::optional<std::string> ProjectPath;
    std::optional<std::string> Document;
    std::unique_ptr<ShojiServices> Services;
};
