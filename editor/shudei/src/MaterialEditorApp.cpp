#include "MaterialEditorApp.h"

#include "MaterialEditorServices.h"

#include <app/Engine.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>

MaterialEditorApp::MaterialEditorApp(std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
}

MaterialEditorApp::~MaterialEditorApp() = default;

void MaterialEditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Shudei - Material Editor";
    // The editor is its own ImGui host; a process can hold only one ImGui
    // context over a window, so the engine's default debug overlay must not
    // be created.
    ctx.Config.Console.UiEnabled = false;
}

void MaterialEditorApp::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    Services = std::make_unique<MaterialEditorServices>(engine, *window, ctx.Config,
                                                        std::move(ProjectPath));
}

void MaterialEditorApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Services)
        Services->RegisterSystems(ctx.Schedule);
}

void MaterialEditorApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (Services)
        Services->HandlePlatformEvent(ctx);
}

void MaterialEditorApp::OnShutdown(GameShutdownContext&)
{
    // Release the asset system inside the Game shutdown window, before the
    // engine frees the graphics services its caches borrow.
    Services.reset();
}
