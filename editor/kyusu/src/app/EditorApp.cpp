#include "EditorApp.h"

#include "EditorServices.h"

#include <app/Engine.h>
#include <graphics/vulkan/GraphicsServices.h>
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
    // The editor is its own ImGui host (EditorUiFeature + EditorConsolePanel);
    // a process can hold only one ImGui context over a window, so the engine's
    // default debug overlay must not be created.
    ctx.Config.Console.UiEnabled = false;
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
    // Drain the GPU first. The frame loop returns without waiting, so the last
    // frames it submitted may still be executing, and EditorServices frees ImGui
    // descriptor sets inline as it goes -- thumbnail bindings destroy their sets
    // the moment they are dropped. The renderer's own wait happens far later, in
    // its destructor, which is well after those frees.
    if (GraphicsServices* graphics = GetEngine().TryGraphics())
        graphics->WaitIdle();

    // Tear the editor down inside the Game shutdown window: EditorServices releases
    // the asset system before the engine frees the graphics services its caches
    // borrow.
    Services.reset();
}
