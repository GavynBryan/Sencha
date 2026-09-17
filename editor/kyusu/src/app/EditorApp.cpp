#include "EditorApp.h"

#include "EditorServices.h"

#include <app/Engine.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <platform/PlatformServices.h>
#include <assets/texture/Image.h>
#include <assets/texture/ImageLoader.h>
#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>

#ifndef SENCHA_EDITOR_BRAND_DIR
#define SENCHA_EDITOR_BRAND_DIR "."
#endif

EditorApp::EditorApp(std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
}

EditorApp::~EditorApp() = default;

void EditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Kyusu";
    // The editor draws its own caption; the window keeps the platform frame
    // only where client decorations are unavailable.
    ctx.Config.Window.ClientDecorations = true;
    // The editor is its own ImGui host (EditorUiFeature + EditorConsolePanel);
    // a process can hold only one ImGui context over a window, so the engine's
    // default debug overlay must not be created.
    ctx.Config.Console.UiEnabled = false;
    // An editor is a tool, not an application a player sits in front of: no
    // pause shell, no options page, no Back action reading its Escape.
    ctx.Config.Runtime.ApplicationShell = false;
    // The editor mounts its project's content roots itself, through its own
    // asset stack; the runtime's default mount of `assets` beside the working
    // directory would be a second stack over the wrong directory.
    ctx.Config.Runtime.ContentRoots.clear();
    // Each frame the editor re-uploads every brush wireframe/solid/overlay once per
    // viewport (up to 4) into a single frame-scratch slice. The game's 1 MB default
    // overflows on real scenes (dropped draws look like warped/missing geometry), so
    // give the editor generous headroom.
    ctx.Config.Graphics.FrameScratchBytesPerFrame = 64ull * 1024 * 1024;
}

namespace
{
// Platform branding, not theme art: the icon is the product's and never
// changes with a theme, so it is set once and forgotten. SDL copies the
// surface, which only borrows our pixels, so both are done with here.
void ApplyWindowIcon(SdlWindow& window)
{
    const std::string path = std::string(SENCHA_EDITOR_BRAND_DIR) + "/kyusu-icon.png";
    const std::optional<Image> icon = LoadImageFromFile(path, /*srgb*/ true);
    if (!icon.has_value() || !icon->IsValid())
        return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(icon->Width), static_cast<int>(icon->Height),
                                                 SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<unsigned char*>(icon->Pixels.data()),
                                                 static_cast<int>(icon->Width) * 4);
    if (surface == nullptr)
        return;
    SDL_SetWindowIcon(window.GetHandle(), surface);
    SDL_DestroySurface(surface);
}
}

void EditorApp::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    ApplyWindowIcon(*window);
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
