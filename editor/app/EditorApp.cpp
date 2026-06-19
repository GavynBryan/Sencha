#include "EditorApp.h"

#include "../app/EditorFrameHook.h"
#include "../app/EditorViewportCameraSystem.h"
#include "../input/UiInputGuard.h"
#include "../level/LevelSerialization.h"
#include "../render/EditorRenderFeature.h"
#include "../ui/EditorConsolePanel.h"
#include "../ui/EditorStatusBar.h"
#include "../ui/EditorToolbar.h"
#include "../ui/EditorUiFeature.h"
#include "../ui/InspectorPanel.h"
#include "../ui/MeshEditPanel.h"
#include "../ui/SceneHierarchyPanel.h"
#include "../ui/ToolPalettePanel.h"
#include "../ui/ViewportPanel.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <core/console/ConsoleService.h>
#include <debug/DebugService.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

EditorApp::~EditorApp() = default;

void EditorApp::OnConfigure(GameConfigureContext& ctx)
{
    ctx.Config.Window.Title = "Sencha Editor";
}

void EditorApp::OnStart(GameStartupContext& ctx)
{
    EnginePtr = &GetEngine();
    Engine& engine = *EnginePtr;

    auto& console = engine.Console();
    auto& debug = engine.Debug();
    auto& windows = engine.Platform().Windows;
    auto& instance = engine.Graphics().Instance;
    auto& frames = engine.Graphics().Frames;
    auto& renderer = engine.Graphics().MainRenderer;

    SdlWindow* window = windows.GetPrimaryWindow();
    if (window == nullptr)
        return;
    Window = window;

    RegisterLevelSerializers();

    // Load the project's game module (if any) BEFORE the document is created, so
    // its components are registered when the document's World registers storage.
    LoadGameModule();

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
            // The camera and the UI can't both own the mouse: while a viewport is
            // captured for navigation the cursor is hidden, so ImGui must ignore it.
            if (UiFeature != nullptr)
                UiFeature->SetMouseInputEnabled(!enabled);
        });

    Shortcuts = std::make_unique<ShortcutRegistry>();
    Shortcuts->Register(SDLK_Z, { .Ctrl = true }, [this] { Commands->Undo(); });
    Shortcuts->Register(SDLK_Z, { .Ctrl = true, .Shift = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_Y, { .Ctrl = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_N, { .Ctrl = true }, [this] { NewDocument(); });
    Shortcuts->Register(SDLK_O, { .Ctrl = true }, [this] { RequestOpenDialog(); });
    Shortcuts->Register(SDLK_S, { .Ctrl = true }, [this] { SaveDocument(); });
    Shortcuts->Register(SDLK_V, { .Shift = true }, [this] { Workspace->MeshEdit.CycleElementKind(); });

    Router = std::make_unique<InputRouter>();
    // The UI is the top layer of the input stack: events over an ImGui panel are
    // consumed here before navigation, tools, or shortcuts can act on them. The
    // viewport's 3D region is a passthrough hole — even though it is an ImGui
    // window, input there belongs to the scene, so it is excluded from UI mouse
    // ownership. (The guard adds pointer capture so drags survive crossing panels.)
    Router->AddHandler(MakeUiInputGuard(
        [this]
        {
            UiInputCapture capture = UiFeature != nullptr ? UiFeature->GetInputCapture()
                                                          : UiInputCapture{};
            if (Viewports != nullptr && Viewports->IsViewportRegionHovered())
                capture.Mouse = false;
            return capture;
        }));
    Router->AddHandler([this](const InputEvent& e) { return Navigation->OnInput(e); });
    Router->AddHandler([this](const InputEvent& e) { return Workspace->Dispatcher->OnInput(e); });
    Router->AddHandler([this](const InputEvent& e) { return Shortcuts->OnInput(e); });

    renderer.AddFeature(std::make_unique<EditorRenderFeature>(
        Workspace->Layout,
        Workspace->Document.GetScene(),
        Workspace->Selection,
        Workspace->Preview,
        *Workspace->Manipulators,
        Workspace->Grid));

    auto uiFeature = std::make_unique<EditorUiFeature>(engine, *window, instance, frames);
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]() { if (Commands) Commands->Undo(); },
        [this]() { if (Commands) Commands->Redo(); },
        [this]() { return Commands != nullptr && Commands->CanUndo(); },
        [this]() { return Commands != nullptr && Commands->CanRedo(); });
    UiFeature->SetFileActions(
        [this]() { NewDocument(); },
        [this]() { RequestOpenDialog(); },
        [this]() { SaveDocument(); },
        [this]() { RequestSaveAsDialog(); });

    // Fixed app chrome: top toolbar + bottom status bar. Registered before the
    // panels so the work-area space they reserve is subtracted from the full-bleed
    // viewport panel below.
    Toolbar = std::make_unique<EditorToolbar>(*Workspace->Tools, Workspace->MeshEdit, Workspace->Grid);
    StatusBar = std::make_unique<EditorStatusBar>(
        *Workspace->Tools, Workspace->Layout, Workspace->Selection, Workspace->Grid);
    UiFeature->AddChrome([this] { Toolbar->Draw(); });
    UiFeature->AddChrome([this] { StatusBar->Draw(); });

    auto viewportPanel = std::make_unique<ViewportPanel>(Workspace->Layout, Workspace->Marquee);
    Viewports = viewportPanel.get();
    UiFeature->AddPanel(std::move(viewportPanel));
    auto editorConsole = std::make_unique<EditorConsolePanel>(debug.GetLogSink(), console);
    ConsolePanel = editorConsole.get();
    ConsolePanel->SetVisible(ctx.Config.Console.OpenOnStart);
    UiFeature->AddPanel(std::move(editorConsole));
    UiFeature->AddPanel(std::make_unique<ToolPalettePanel>(*Workspace->Tools));
    UiFeature->AddPanel(std::make_unique<SceneHierarchyPanel>(
        Workspace->Document.GetScene(), Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<InspectorPanel>(
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<MeshEditPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands));

    renderer.AddFeature(std::move(uiFeature));
}

void EditorApp::OnRegisterSystems(SystemRegisterContext& ctx)
{
    if (Workspace == nullptr)
        return;

    CameraSystem = &ctx.Schedule.Register<EditorViewportCameraSystem>(Workspace->Layout);
    FrameHook = &ctx.Schedule.Register<EditorFrameHook>([this] { OnFrame(); });
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

    if (ctx.Event.type == SDL_EVENT_KEY_DOWN
        && !ctx.Event.key.repeat
        && ctx.Event.key.scancode == SDL_SCANCODE_GRAVE)
    {
        if (ConsolePanel != nullptr)
            ConsolePanel->ToggleVisible();
        ctx.Handled = true;
        return;
    }

    if (UiFeature != nullptr)
        UiFeature->ProcessSdlEvent(ctx.Event);

    if (Router != nullptr)
    {
        // Uniform routing: the UI-capture guard at the head of the chain decides
        // whether the UI owns this event (mouse or keyboard), so there is no
        // per-device special-casing here.
        const std::optional<InputEvent> event = TranslateEvent(ctx.Event);
        if (event.has_value() && Router->Route(*event) == InputConsumed::Yes)
            ctx.Handled = true;
    }
}

void EditorApp::OnShutdown(GameShutdownContext& ctx)
{
    if (EnginePtr != nullptr)
        SetRelativeMouseMode(*EnginePtr, false);

    CameraSystem = nullptr;
    FrameHook = nullptr;
    Viewports = nullptr;
    ConsolePanel = nullptr;
    UiFeature = nullptr;
    Window = nullptr;
    UnloadGameModule();
    Workspace.reset();
    Commands.reset();
    Router.reset();
    Navigation.reset();
    Shortcuts.reset();
    EnginePtr = nullptr;
}

namespace
{
constexpr SDL_DialogFileFilter kLevelFileFilters[] = {
    { "Sencha Level", "json" },
    { "All files", "*" },
};
} // namespace

void EditorApp::NewDocument()
{
    if (Workspace == nullptr)
        return;

    Workspace->Document.New();
    ResetEditorState();
}

void EditorApp::SaveDocument()
{
    if (Workspace == nullptr)
        return;

    if (!Workspace->Document.HasFilePath())
    {
        RequestSaveAsDialog();
        return;
    }

    Workspace->Document.Save();
}

void EditorApp::RequestOpenDialog()
{
    if (Window == nullptr || Window->GetHandle() == nullptr)
        return;

    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            auto* app = static_cast<EditorApp*>(userdata);
            if (filelist != nullptr && filelist[0] != nullptr)
                app->EnqueueFileAction(FileActionKind::Open, filelist[0]);
        },
        this,
        Window->GetHandle(),
        kLevelFileFilters,
        static_cast<int>(std::size(kLevelFileFilters)),
        nullptr,
        false);
}

void EditorApp::RequestSaveAsDialog()
{
    if (Window == nullptr || Window->GetHandle() == nullptr)
        return;

    SDL_ShowSaveFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            auto* app = static_cast<EditorApp*>(userdata);
            if (filelist != nullptr && filelist[0] != nullptr)
                app->EnqueueFileAction(FileActionKind::SaveAs, filelist[0]);
        },
        this,
        Window->GetHandle(),
        kLevelFileFilters,
        static_cast<int>(std::size(kLevelFileFilters)),
        nullptr);
}

void EditorApp::EnqueueFileAction(FileActionKind kind, std::string path)
{
    const std::scoped_lock lock(PendingFileMutex);
    PendingFileActions.push_back({ kind, std::move(path) });
}

void EditorApp::ProcessPendingFileActions()
{
    std::vector<PendingFileAction> actions;
    {
        const std::scoped_lock lock(PendingFileMutex);
        actions.swap(PendingFileActions);
    }

    if (Workspace == nullptr)
        return;

    for (const PendingFileAction& action : actions)
    {
        switch (action.Kind)
        {
        case FileActionKind::Open:
            if (Workspace->Document.Load(action.Path))
                ResetEditorState();
            break;
        case FileActionKind::SaveAs:
            Workspace->Document.SaveAs(action.Path);
            break;
        }
    }
}

void EditorApp::ResetEditorState()
{
    if (Commands != nullptr)
        Commands->Clear();
    if (Workspace != nullptr)
        Workspace->Selection.ClearSelection();
}

void EditorApp::UpdateWindowTitle()
{
    if (Window == nullptr || Workspace == nullptr)
        return;

    std::string title = "Sencha Editor - ";
    title += Workspace->Document.GetDisplayName();
    if (Workspace->Document.IsDirty())
        title += " *";

    if (title != LastWindowTitle)
    {
        Window->SetTitle(title);
        LastWindowTitle = title;
    }
}

void EditorApp::OnFrame()
{
    ProcessPendingFileActions();
    UpdateWindowTitle();
}

void EditorApp::LoadGameModule()
{
    const char* path = std::getenv("SENCHA_GAME_MODULE");
    if (path == nullptr || path[0] == '\0')
        return;

    auto& console = EnginePtr->Console();
    GameModuleContext ctx{ DefaultComponentSerializerRegistry(), console.Registry(), HostInfo };
    std::string error;
    GameModule = ModuleLoader.Load(path, ctx, &error);
    if (!GameModule.IsValid())
    {
        std::fprintf(stderr, "[editor] failed to load game module '%s': %s\n",
                     path, error.c_str());
        return;
    }

    const std::string_view name = GameModule.Module->Name();
    std::fprintf(stderr, "[editor] loaded game module '%s' (%.*s)\n",
                 path, static_cast<int>(name.size()), name.data());
}

void EditorApp::UnloadGameModule()
{
    if (!GameModule.IsValid())
        return;

    auto& console = EnginePtr->Console();
    GameModuleContext ctx{ DefaultComponentSerializerRegistry(), console.Registry(), HostInfo };
    ModuleLoader.Unload(GameModule, ctx);
}

void EditorApp::SetRelativeMouseMode(Engine& engine, bool enabled)
{
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
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
