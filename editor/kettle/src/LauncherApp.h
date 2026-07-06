#pragma once

#include <app/Game.h>

#include <memory>

class LauncherServices;

// Kettle's Game entry point: owns the LauncherServices composition root and
// forwards each Game lifecycle hook to it.
class LauncherApp : public Game
{
public:
    LauncherApp();
    ~LauncherApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    std::unique_ptr<LauncherServices> Services;
};
