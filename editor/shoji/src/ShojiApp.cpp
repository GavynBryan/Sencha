#include "ShojiApp.h"

#include "ShojiServices.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

ShojiApp::ShojiApp(std::optional<std::string> projectPath, std::optional<std::string> document)
    : ProjectPath(std::move(projectPath))
    , Document(std::move(document))
{
}

ShojiApp::~ShojiApp() = default;

void ShojiApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.App.Name = "Shoji";
    ctx.Config.Window.Title = "Shoji - UI Previewer";
    // The editor draws its own caption; the window keeps the platform frame
    // only where client decorations are unavailable.
    ctx.Config.Window.ClientDecorations = true;
    // The editor is its own ImGui host; a process can hold only one ImGui
    // context over a window, so the engine's default debug overlay must not
    // be created.
    ctx.Config.Console.UiEnabled = false;
    // A tool, not an application a player sits in front of: no pause shell,
    // no options page, no Back action reading its Escape. The documents it
    // previews are opened on its own surface, never the shell's.
    ctx.Config.Runtime.ApplicationShell = false;
    // The previewer mounts what it previews itself; the runtime's default
    // mount of `assets` beside the working directory would be a second stack
    // over the wrong directory.
    ctx.Config.Runtime.ContentRoots.clear();
}

void ShojiApp::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;
    Services = std::make_unique<ShojiServices>(engine, *window, ctx.Config,
                                               std::move(ProjectPath), std::move(Document));
}

void ShojiApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Services)
        Services->RegisterSystems(ctx.Schedule);
}

void ShojiApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (Services)
        Services->HandlePlatformEvent(ctx);
}

void ShojiApp::OnShutdown(GameShutdownContext&)
{
    // Drain the GPU first: the preview target's ImGui descriptor set is freed
    // inline as the services go away.
    if (GraphicsServices* graphics = GetEngine().TryGraphics())
        graphics->WaitIdle();
    Services.reset();
}
