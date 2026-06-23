#include "EditorApp.h"

#include "EditorServices.h"

#include <app/Engine.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

#include <memory>

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

void EditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Sencha Editor";
}

void EditorApp::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    Services = std::make_unique<EditorServices>(engine, *window, ctx.Config);
}

void EditorApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Services)
        Services->RegisterSystems(ctx.Schedule);
}

void EditorApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (Services)
        Services->HandlePlatformEvent(ctx);
}

void EditorApp::OnShutdown(GameShutdownContext&)
{
    // Tear the editor down inside the Game shutdown window: EditorServices releases
    // the asset system before the engine frees the graphics services its caches
    // borrow.
    Services.reset();
}
