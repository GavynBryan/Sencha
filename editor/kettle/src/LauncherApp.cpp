#include "LauncherApp.h"

#include "LauncherServices.h"

#include <app/Engine.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

LauncherApp::LauncherApp() = default;
LauncherApp::~LauncherApp() = default;

void LauncherApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Kettle - Project Launcher";
    // The editor is its own ImGui host; a process can hold only one ImGui
    // context over a window, so the engine's default debug overlay must not
    // be created.
    ctx.Config.Console.UiEnabled = false;
}

void LauncherApp::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    Services = std::make_unique<LauncherServices>(engine, *window);
}

void LauncherApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Services)
        Services->RegisterSystems(ctx.Schedule);
}

void LauncherApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (Services)
        Services->HandlePlatformEvent(ctx);
}

void LauncherApp::OnShutdown(GameShutdownContext&)
{
    Services.reset();
}
