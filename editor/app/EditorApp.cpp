#include "EditorApp.h"

#include "../app/EditorViewportCameraSystem.h"
#include "../level/tools/SelectTool.h"
#include "../render/EditorRenderFeature.h"
#include "../ui/EditorUiFeature.h"
#include "../ui/ToolPalettePanel.h"
#include "../ui/ViewportPanel.h"
#include "../viewport/FourWayViewportLayout.h"

#include <app/Engine.h>
#include <core/service/ServiceHost.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

#include <memory>

EditorApp::~EditorApp() = default;

void EditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Sencha Editor";
}

void EditorApp::OnStart(GameStartupContext& ctx)
{
    ServiceHost& services = ctx.EngineInstance.Services();
    auto& windows = services.Get<SdlWindowService>();
    auto& instance = services.Get<VulkanInstanceService>();
    auto& frames = services.Get<VulkanFrameService>();
    auto& renderer = services.Get<Renderer>();

    SdlWindow* window = windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    ViewportLayout = std::make_unique<FourWayViewportLayout>();
    ViewportLayout->OnResize(window->GetExtent().Width, window->GetExtent().Height);

    Commands = std::make_unique<CommandStack>();
    Selection = std::make_unique<SelectionContext>();
    SelectionState = std::make_unique<SelectionService>(*Selection);
    Picking = std::make_unique<PickingService>();
    ToolState = std::make_unique<ToolContext>(*Commands, *SelectionState, *Picking);
    Tools = std::make_unique<ToolRegistry>(*ToolState);
    Tools->Register(std::make_unique<SelectTool>());
    Tools->Activate("select");

    renderer.AddFeature(std::make_unique<EditorRenderFeature>(*ViewportLayout));

    auto uiFeature = std::make_unique<EditorUiFeature>(ctx.EngineInstance, *window, instance, frames);
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]()
        {
            if (Commands != nullptr)
                Commands->Undo();
        },
        [this]()
        {
            if (Commands != nullptr)
                Commands->Redo();
        },
        [this]()
        {
            return Commands != nullptr && Commands->CanUndo();
        },
        [this]()
        {
            return Commands != nullptr && Commands->CanRedo();
        });

    auto viewportPanel = std::make_unique<ViewportPanel>(*ViewportLayout, *Tools);
    Viewports = viewportPanel.get();
    UiFeature->AddPanel(std::move(viewportPanel));
    UiFeature->AddPanel(std::make_unique<ToolPalettePanel>(*Tools));

    renderer.AddFeature(std::move(uiFeature));
}

void EditorApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (ViewportLayout == nullptr)
        return;

    CameraSystem = &ctx.Schedule.Register<EditorViewportCameraSystem>(*ViewportLayout);
}

void EditorApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (ViewportLayout != nullptr)
    {
        switch (ctx.Event.type)
        {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            ViewportLayout->OnResize(
                static_cast<uint32_t>(ctx.Event.window.data1),
                static_cast<uint32_t>(ctx.Event.window.data2));
            break;
        default:
            break;
        }
    }

    const bool viewportHandled = Viewports != nullptr && Viewports->ProcessSdlEvent(ctx.Event);
    if (viewportHandled)
        ctx.Handled = true;

    if (ViewportLayout != nullptr)
    {
        if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            && ctx.Event.button.button == SDL_BUTTON_RIGHT)
        {
            EditorViewport* activeViewport = ViewportLayout->GetActiveViewport();
            const bool enableRelativeMode = activeViewport != nullptr
                && activeViewport->WantsFlyCameraInput
                && activeViewport->Camera.ActiveMode == EditorCamera::Mode::Perspective;
            SetRelativeMouseMode(ctx.EngineInstance, enableRelativeMode);
        }
        else if ((ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
                  && ctx.Event.button.button == SDL_BUTTON_RIGHT)
                 || ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            SetRelativeMouseMode(ctx.EngineInstance, false);
        }
    }

    if (UiFeature != nullptr && UiFeature->ProcessSdlEvent(ctx.Event))
        ctx.Handled = true;

    if (ctx.Handled || Commands == nullptr || ctx.Event.type != SDL_EVENT_KEY_DOWN)
        return;

    const bool ctrl = (ctx.Event.key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (ctx.Event.key.mod & SDL_KMOD_SHIFT) != 0;
    if (!ctrl)
        return;

    switch (ctx.Event.key.key)
    {
    case SDLK_Z:
        if (shift)
            Commands->Redo();
        else
            Commands->Undo();
        ctx.Handled = true;
        break;

    case SDLK_Y:
        Commands->Redo();
        ctx.Handled = true;
        break;

    default:
        break;
    }
}

void EditorApp::OnShutdown(GameShutdownContext& ctx)
{
    SetRelativeMouseMode(ctx.EngineInstance, false);
    CameraSystem = nullptr;
    Viewports = nullptr;
    ViewportLayout.reset();
    UiFeature = nullptr;
    Tools.reset();
    ToolState.reset();
    Picking.reset();
    SelectionState.reset();
    Selection.reset();
    Commands.reset();
}

void EditorApp::SetRelativeMouseMode(Engine& engine, bool enabled)
{
    SdlWindow* window = engine.Services().Get<SdlWindowService>().GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;

    const bool relativeMouseModeChanged = SDL_GetWindowRelativeMouseMode(window->GetHandle()) != enabled;
    if (relativeMouseModeChanged)
        SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);

    SDL_CaptureMouse(enabled);
    if (enabled)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}
