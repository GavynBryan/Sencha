#include "DataEditorApp.h"

#include "DataEditorServices.h"

#include <app/Engine.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

#include <utility>

DataEditorApp::DataEditorApp(std::optional<std::string> projectPath,
                             std::optional<std::string> initialAsset)
    : ProjectPath(std::move(projectPath))
    , InitialAsset(std::move(initialAsset))
{
}

DataEditorApp::~DataEditorApp() = default;

void DataEditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Data Editor";
    // The editor is its own ImGui host; a process can hold only one ImGui
    // context over a window, so the engine's default debug overlay must not
    // be created.
    ctx.Config.Console.UiEnabled = false;
}

void DataEditorApp::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    Services = std::make_unique<DataEditorServices>(
        engine, *window, ctx.Config, std::move(ProjectPath), std::move(InitialAsset));
}

void DataEditorApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Services)
        Services->RegisterSystems(ctx.Schedule);
}

void DataEditorApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (Services)
        Services->HandlePlatformEvent(ctx);
}

void DataEditorApp::OnShutdown(GameShutdownContext&)
{
    Services.reset();
}
