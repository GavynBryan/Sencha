#include "EditorApp.h"

#include "../app/EditorViewportCameraSystem.h"
#include "../render/EditorRenderFeature.h"
#include "../ui/EditorUiFeature.h"
#include "../ui/ToolPalettePanel.h"
#include "../ui/ViewportPanel.h"
#include "../viewport/FourWayViewportLayout.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <core/service/ServiceHost.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

#include <memory>
#include <optional>

EditorApp::~EditorApp() = default;

void EditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Sencha Editor";
}

void EditorApp::OnStart(GameStartupContext& ctx)
{
    EnginePtr = &ctx.EngineInstance;

    ServiceHost& services = ctx.EngineInstance.Services();
    auto& windows = services.Get<SdlWindowService>();
    auto& instance = services.Get<VulkanInstanceService>();
    auto& frames = services.Get<VulkanFrameService>();
    auto& renderer = services.Get<Renderer>();

    SdlWindow* window = windows.GetPrimaryWindow();
    if (window == nullptr)
        return;

    Commands = std::make_unique<CommandStack>();
    Workspace = std::make_unique<LevelWorkspace>();
    Workspace->Layout.OnResize(window->GetExtent().Width, window->GetExtent().Height);
    Workspace->Init(*Commands);

    Navigation = std::make_unique<ViewportNavigation>(
        Workspace->Layout,
        [this](bool enabled)
        {
            if (EnginePtr != nullptr)
                SetRelativeMouseMode(*EnginePtr, enabled);
        });

    Shortcuts = std::make_unique<ShortcutRegistry>();
    Shortcuts->Register(SDLK_Z, { .Ctrl = true }, [this] { Commands->Undo(); });
    Shortcuts->Register(SDLK_Z, { .Ctrl = true, .Shift = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_Y, { .Ctrl = true }, [this] { Commands->Redo(); });

    Router = std::make_unique<InputRouter>();
    Router->AddHandler([this](const InputEvent& e) { return Navigation->OnInput(e); });
    Router->AddHandler([this](const InputEvent& e) { return Workspace->Tools->OnInput(e); });
    Router->AddHandler([this](const InputEvent& e) { return Shortcuts->OnInput(e); });

    renderer.AddFeature(std::make_unique<EditorRenderFeature>(
        Workspace->Layout,
        Workspace->Document.GetScene(),
        Workspace->Selection));

    auto uiFeature = std::make_unique<EditorUiFeature>(ctx.EngineInstance, *window, instance, frames);
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]() { if (Commands) Commands->Undo(); },
        [this]() { if (Commands) Commands->Redo(); },
        [this]() { return Commands != nullptr && Commands->CanUndo(); },
        [this]() { return Commands != nullptr && Commands->CanRedo(); });

    auto viewportPanel = std::make_unique<ViewportPanel>(Workspace->Layout, *Workspace->Tools);
    Viewports = viewportPanel.get();
    UiFeature->AddPanel(std::move(viewportPanel));
    UiFeature->AddPanel(std::make_unique<ToolPalettePanel>(*Workspace->Tools));

    renderer.AddFeature(std::move(uiFeature));
}

void EditorApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Workspace == nullptr)
        return;

    CameraSystem = &ctx.Schedule.Register<EditorViewportCameraSystem>(Workspace->Layout);
}

void EditorApp::OnPlatformEvent(PlatformEventContext& ctx)
{
    switch (ctx.Event.type)
    {
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (Workspace != nullptr)
            Workspace->Layout.OnResize(
                static_cast<uint32_t>(ctx.Event.window.data1),
                static_cast<uint32_t>(ctx.Event.window.data2));
        break;
    default:
        break;
    }

    if (UiFeature != nullptr)
        UiFeature->ProcessSdlEvent(ctx.Event);

    if (Router != nullptr)
    {
        const std::optional<InputEvent> event = TranslateEvent(ctx.Event);
        if (event.has_value())
        {
            const bool isKeyboard = std::holds_alternative<KeyDownEvent>(*event);
            const bool imguiBlocksKeyboard = isKeyboard && ImGui::GetIO().WantCaptureKeyboard;
            if (!imguiBlocksKeyboard && Router->Route(*event) == InputConsumed::Yes)
                ctx.Handled = true;
        }
    }
}

void EditorApp::OnShutdown(GameShutdownContext& ctx)
{
    if (EnginePtr != nullptr)
        SetRelativeMouseMode(*EnginePtr, false);

    CameraSystem = nullptr;
    Viewports = nullptr;
    UiFeature = nullptr;
    Workspace.reset();
    Commands.reset();
    Router.reset();
    Navigation.reset();
    Shortcuts.reset();
    EnginePtr = nullptr;
}

void EditorApp::SetRelativeMouseMode(Engine& engine, bool enabled)
{
    SdlWindow* window = engine.Services().Get<SdlWindowService>().GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;

    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) != enabled)
        SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);

    SDL_CaptureMouse(enabled);
    if (enabled)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}

ModifierFlags EditorApp::ReadModifiers(SDL_Keymod mod)
{
    return {
        .Ctrl = (mod & SDL_KMOD_CTRL) != 0,
        .Shift = (mod & SDL_KMOD_SHIFT) != 0,
        .Alt = (mod & SDL_KMOD_ALT) != 0,
    };
}

std::optional<InputEvent> EditorApp::TranslateEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        MouseButton button;
        if (event.button.button == SDL_BUTTON_LEFT)
            button = MouseButton::Left;
        else if (event.button.button == SDL_BUTTON_RIGHT)
            button = MouseButton::Right;
        else if (event.button.button == SDL_BUTTON_MIDDLE)
            button = MouseButton::Middle;
        else
            return std::nullopt;

        return PointerDownEvent{
            .Position = { event.button.x, event.button.y },
            .Button = button,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        MouseButton button;
        if (event.button.button == SDL_BUTTON_LEFT)
            button = MouseButton::Left;
        else if (event.button.button == SDL_BUTTON_RIGHT)
            button = MouseButton::Right;
        else if (event.button.button == SDL_BUTTON_MIDDLE)
            button = MouseButton::Middle;
        else
            return std::nullopt;

        return PointerUpEvent{
            .Position = { event.button.x, event.button.y },
            .Button = button,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };
    }

    case SDL_EVENT_MOUSE_MOTION:
        return PointerMoveEvent{
            .Position = { event.motion.x, event.motion.y },
            .Delta = { event.motion.xrel, event.motion.yrel },
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };

    case SDL_EVENT_MOUSE_WHEEL:
        return WheelEvent{
            .Position = {},
            .Delta = event.wheel.y,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };

    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat)
            return std::nullopt;
        return KeyDownEvent{
            .Key = event.key.key,
            .Modifiers = ReadModifiers(event.key.mod),
        };

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return FocusLostEvent{};

    default:
        return std::nullopt;
    }
}
