#include "EditorApp.h"

#include "EditorServices.h"

#include <app/Engine.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

#include <memory>

EditorApp::EditorApp(std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
}

EditorApp::~EditorApp() = default;

void EditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Kyusu - Level Editor";
    // Each frame the editor re-uploads every brush wireframe/solid/overlay once per
    // viewport (up to 4) into a single frame-scratch slice. The game's 1 MB default
    // overflows on real scenes (dropped draws look like warped/missing geometry), so
    // give the editor generous headroom.
    ctx.Config.Graphics.FrameScratchBytesPerFrame = 64ull * 1024 * 1024;
}

void EditorApp::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    Services = std::make_unique<EditorServices>(engine, *window, ctx.Config, std::move(ProjectPath));
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
