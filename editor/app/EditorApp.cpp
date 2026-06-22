#include "EditorApp.h"

#include "../app/EditorFrameHook.h"
#include "../viewport/EditorViewportCameraSystem.h"
#include "../input/UiInputGuard.h"
#include "../level/LevelCook.h"
#include "../level/LevelSerialization.h"
#include "../level/MaterialLibrary.h"
#include "../render/EditorRenderFeature.h"
#include "../ui/EditorConsolePanel.h"
#include "../ui/EditorStatusBar.h"
#include "../ui/EditorToolbar.h"
#include "../ui/EditorUiFeature.h"
#include "../ui/InspectorPanel.h"
#include "../ui/MaterialPanel.h"
#include "../ui/MeshEditPanel.h"
#include "../ui/SceneHierarchyPanel.h"
#include "../ui/ToolPalettePanel.h"
#include "../ui/ViewportPanel.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <core/logging/Logger.h>
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
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

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

    // Build the asset system and mount the project content (needs the project from
    // LoadGameModule). The document then serializes through it.
    InitAssets();

    // The author -> cook -> play loop runs through these console commands
    // (`cook`, `play`, `stop`) and the cook cell-size cvar.
    RegisterEditorCommands();

    Commands = std::make_unique<CommandStack>();
    Workspace = std::make_unique<LevelWorkspace>(engine.Logging());
    if (Assets)
        Workspace->Document.SetAssetEnvironment(*Assets);
    Workspace->Layout.OnResize(window->GetExtent().Width, window->GetExtent().Height);
    Workspace->Init(*Commands);

    Navigation = std::make_unique<ViewportNavigation>(
        Workspace->Layout,
        [this](bool enabled)
        {
            // Fly-look only: hide the cursor and switch to relative mouse. The ImGui
            // mouse gate is driven by pointer capture (Router->SetCaptureChanged
            // below), which also covers ortho-pan and tool drags.
            if (EnginePtr != nullptr)
                SetRelativeMouseMode(*EnginePtr, enabled);
        });

    Shortcuts = std::make_unique<ShortcutRegistry>();
    Shortcuts->Register(SDLK_Z, { .Ctrl = true }, [this] { Commands->Undo(); });
    Shortcuts->Register(SDLK_Z, { .Ctrl = true, .Shift = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_Y, { .Ctrl = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_DELETE, {}, [this] { Workspace->DeleteSelection(); });
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
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Navigation->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Workspace->Dispatcher->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture&) { return Shortcuts->OnInput(e); });

    // The pointer's owner drives the ImGui input gate: while a viewport gesture
    // (fly-look, ortho-pan, or a tool drag) holds capture, ImGui ignores both mouse
    // and keyboard, so the unowned/hidden cursor can't hover or click the UI and the
    // fly camera's WASD/QE don't leak into a focused widget (the console input). A UI
    // drag (kind != Viewport) keeps both on.
    Router->SetCaptureChanged(
        [this](std::optional<PointerCaptureKind> kind)
        {
            if (UiFeature != nullptr)
            {
                const bool uiOwnsInput = kind != PointerCaptureKind::Viewport;
                UiFeature->SetMouseInputEnabled(uiOwnsInput);
                UiFeature->SetKeyboardInputEnabled(uiOwnsInput);
            }
        });

    renderer.AddFeature(std::make_unique<EditorRenderFeature>(
        Workspace->Layout,
        Workspace->Document.GetScene(),
        Workspace->Selection,
        Workspace->Preview,
        *Workspace->Manipulators,
        Workspace->Grid,
        engine.Logging(),
        engine.Console().Registry(),
        Assets ? &Assets->Assets : nullptr,
        Assets ? &Assets->Registry : nullptr));

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
    Toolbar = std::make_unique<EditorToolbar>(*Workspace->Tools, Workspace->MeshEdit, Workspace->Grid,
                                              Workspace->BrushCreate);
    // The Cook/Play/Stop group routes through the same paths as the cook/play/stop
    // console commands.
    Toolbar->SetPlayControls({
        .Cook = [this] { CookCurrentLevel(""); },
        .Play = [this] { StartPlaySession(LastCookedMap); },
        .Stop = [this] { Pie.Stop(); },
        .IsPlaying = [this] { return Pie.IsRunning(); },
    });
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
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<InspectorPanel>(
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<MeshEditPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands));
    Materials = std::make_unique<MaterialLibrary>(engine.Logging());
    UiFeature->AddPanel(std::make_unique<MaterialPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands,
        *Materials, Workspace->Document));

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

    if (Router != nullptr && Workspace != nullptr)
    {
        // Uniform routing: the UI-capture guard at the head of the chain decides
        // whether the UI owns this event (mouse or keyboard), so there is no
        // per-device special-casing here. Pointer events are stamped with their
        // origin viewport first, so navigation and tools never re-resolve it.
        std::optional<InputEvent> event = TranslateEvent(ctx.Event);
        if (event.has_value())
        {
            StampOriginViewport(*event);
            if (Router->Route(*event) == InputConsumed::Yes)
                ctx.Handled = true;
        }
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
    // After Workspace: the document's StaticMeshComponents release into these caches
    // on teardown. Before the engine frees the graphics services the caches borrow.
    Assets.reset();
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
            {
                ResetEditorState();
                RescanMaterials(action.Path);
            }
            break;
        case FileActionKind::SaveAs:
            Workspace->Document.SaveAs(action.Path);
            RescanMaterials(action.Path);
            break;
        }
    }
}

void EditorApp::RescanMaterials(const std::string& levelPath)
{
    // Materials are project-relative: scan the directory holding the level file
    // so face textures resolve to the same asset:// paths the runtime will use.
    if (Materials == nullptr)
        return;
    const std::filesystem::path dir = std::filesystem::path(levelPath).parent_path();
    Materials->Rescan(dir.string());
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
    // Prefer a project descriptor (SENCHA_PROJECT); fall back to a bare module
    // path (SENCHA_GAME_MODULE) so the pre-project workflow still works.
    std::string modulePath;
    if (const char* projectPath = std::getenv("SENCHA_PROJECT");
        projectPath != nullptr && projectPath[0] != '\0')
    {
        ProjectDescriptor descriptor;
        std::string error;
        if (!ProjectDescriptor::Load(projectPath, descriptor, &error))
        {
            std::fprintf(stderr, "[editor] failed to open project '%s': %s\n",
                         projectPath, error.c_str());
            return;
        }
        Project = std::move(descriptor);
        modulePath = Project->GameModulePath;
        std::fprintf(stderr, "[editor] opened project '%s' (%s)\n",
                     Project->Name.c_str(), projectPath);
    }
    else if (const char* envPath = std::getenv("SENCHA_GAME_MODULE");
             envPath != nullptr && envPath[0] != '\0')
    {
        modulePath = envPath;
    }

    if (modulePath.empty())
        return;

    std::string error;
    GameModule = ModuleLoader.Load(modulePath, &error);
    if (!GameModule.IsValid())
    {
        std::fprintf(stderr, "[editor] failed to load game module '%s': %s\n",
                     modulePath.c_str(), error.c_str());
        return;
    }

    // The editor only borrows the module's component serializers (so it can edit
    // scenes containing game components); it never runs the game's lifecycle.
    GameModule.Instance->OnRegisterComponents(DefaultComponentSerializerRegistry());
    std::fprintf(stderr, "[editor] loaded game module '%s'\n", modulePath.c_str());
}

void EditorApp::InitAssets()
{
    if (EnginePtr == nullptr)
        return;
    Engine& engine = *EnginePtr;
    GraphicsServices& graphics = engine.Graphics();
    LoggingProvider& logging = engine.Logging();

    Assets.emplace(logging, graphics.Buffers, graphics.Images, graphics.Descriptors, graphics.Samplers);
    if (!Project)
        return;

    // Mount each content root and its cooked overlay, then apply that root's asset
    // id map. The same resolution the runtime uses, so an authored ref the editor
    // resolves is the one the cook stamps and the runtime loads.
    Logger& log = logging.GetLogger<EditorApp>();
    for (const std::string& root : Project->ContentRoots)
    {
        ScanAssetsDirectory(root, Assets->Registry);
        ScanAssetsDirectory((std::filesystem::path(root) / ".cooked").string(), Assets->Registry);

        AssetIdMap idMap;
        std::string idMapError;
        const std::string idMapPath = (std::filesystem::path(root) / kAssetIdMapFileName).string();
        if (AssetIdMap::LoadFromFile(idMapPath, idMap, &idMapError))
            ApplyAssetIds(idMap, Assets->Registry);
    }
    log.Info("assets: mounted {} content root(s)", Project->ContentRoots.size());
}

std::string EditorApp::ResolveHostAppPath() const
{
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return "app";

    // weakly_canonical drops SDL's trailing slash so parent_path is the real parent.
    const std::filesystem::path baseDir = std::filesystem::weakly_canonical(base);

    // Installed SDK: app sits beside the editor (bin/app, bin/sencha_editor).
    std::filesystem::path candidate = baseDir / "app";
    if (std::filesystem::exists(candidate))
        return candidate.string();

    // Build tree: editor is build/editor/, app is build/app/.
    candidate = baseDir.parent_path() / "app" / "app";
    if (std::filesystem::exists(candidate))
        return candidate.string();

    return (baseDir / "app").string();
}

void EditorApp::StartPlaySession(const std::string& map)
{
    if (EnginePtr == nullptr)
        return;
    Logger& log = EnginePtr->Logging().GetLogger<EditorApp>();

    if (!Project)
    {
        log.Error("play: no project open (set SENCHA_PROJECT)");
        return;
    }
    if (map.empty())
    {
        log.Error("play: no cooked map; cook a level first or pass a map name");
        return;
    }

    const std::string app = ResolveHostAppPath();
    // CWD is the project directory: the game resolves its content roots
    // ("assets", "assets/.cooked") relative to it, exactly as a shipped game.
    log.Info("play: {} --game {} +map {} (cwd {})",
             app, Project->GameModulePath, map, Project->Directory);

    std::string error;
    if (!Pie.Launch(app, Project->GameModulePath, Project->Directory, map, &error))
        log.Error("play failed: " + error);
    else
        log.Info("play: session started (" + map + ")");
}

void EditorApp::RegisterEditorCommands()
{
    if (EnginePtr == nullptr)
        return;

    ConsoleRegistry& registry = EnginePtr->Console().Registry();

    registry.RegisterCVar({
        .Name = "editor.cook.cell_size",
        .Owner = "editor",
        .Type = CVarType::Double,
        .DefaultValue = 16.0,
        .CurrentValue = 16.0,
        .Flags = CVarFlags::Archive,
        .Help = "World-space grid size the level cook clusters brushes into per-cell meshes.",
        .Source = { "editor" },
        .Min = 0.0,
    });

    registry.RegisterCVar({
        .Name = "editor.cull_backfaces",
        .Owner = "editor",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        .Flags = CVarFlags::Archive,
        .Help = "Backface-cull the editor solid viewport to match play mode.",
        .Source = { "editor" },
    });

    ConsoleCommandMetadata cook;
    cook.Name = "cook";
    cook.Owner = "editor";
    cook.Usage = "cook [name]";
    cook.Help = "Cook the live level into the project's assets (name defaults to the document).";
    cook.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        ConsoleResult result;
        const std::string name = args.empty() ? std::string{} : args.front();
        const std::string map = CookCurrentLevel(name);
        if (map.empty())
            result.Error("cook failed (see log)");
        else
            result.Info("cooked " + map);
        return result;
    };
    registry.RegisterCommand(std::move(cook));

    ConsoleCommandMetadata play;
    play.Name = "play";
    play.Owner = "editor";
    play.Usage = "play [map]";
    play.Help = "Launch the project in the app host (PIE); map defaults to the last cooked level.";
    play.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        const std::string map = args.empty() ? LastCookedMap : args.front();
        StartPlaySession(map);
        return ConsoleResult{};
    };
    registry.RegisterCommand(std::move(play));

    ConsoleCommandMetadata stop;
    stop.Name = "stop";
    stop.Owner = "editor";
    stop.Usage = "stop";
    stop.Help = "Stop the running PIE session.";
    stop.Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
        ConsoleResult result;
        if (!Pie.IsRunning())
        {
            result.Info("no play session running");
            return result;
        }
        Pie.Stop();
        result.Info("stopped play session");
        return result;
    };
    registry.RegisterCommand(std::move(stop));

    ConsoleCommandMetadata project;
    project.Name = "project";
    project.Owner = "editor";
    project.Usage = "project <info|save|new <dir> [name]>";
    project.Help = "Inspect, save, or create a project descriptor (.senchaproj).";
    project.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        ConsoleResult result;
        const std::string verb = args.empty() ? "info" : args.front();
        if (verb == "info")
        {
            if (!Project)
                result.Info("no project open (set SENCHA_PROJECT)");
            else
                result.Info("project '" + Project->Name + "' @ " + Project->Directory);
        }
        else if (verb == "save")
        {
            if (!Project)
            {
                result.Error("no project open");
                return result;
            }
            std::string error;
            const std::string path =
                (std::filesystem::path(Project->Directory) / "project.senchaproj").string();
            if (!Project->Save(path, &error))
                result.Error("save failed: " + error);
            else
                result.Info("saved " + path);
        }
        else if (verb == "new")
        {
            if (args.size() < 2)
            {
                result.Error("usage: project new <dir> [name]");
                return result;
            }
            ProjectDescriptor created;
            std::string error;
            const std::string name = args.size() >= 3 ? args[2] : std::string{};
            if (!ProjectDescriptor::Create(args[1], name, created, &error))
                result.Error("create failed: " + error);
            else
                result.Info("created project at " + created.Directory);
        }
        else
        {
            result.Error("unknown verb '" + verb + "'");
        }
        return result;
    };
    registry.RegisterCommand(std::move(project));
}

std::string EditorApp::CookCurrentLevel(const std::string& levelName)
{
    if (EnginePtr == nullptr || Workspace == nullptr)
        return {};
    Logger& log = EnginePtr->Logging().GetLogger<EditorApp>();
    if (!Project)
    {
        log.Error("cook: no project open (set SENCHA_PROJECT)");
        return {};
    }

    // Name the artifacts after the explicit arg, else the document's file stem,
    // else "untitled" for a never-saved level.
    std::string name = levelName;
    if (name.empty())
    {
        const std::filesystem::path docPath(Workspace->Document.GetDisplayName());
        name = Workspace->Document.HasFilePath() ? docPath.stem().string() : "untitled";
    }

    double cellSize = 16.0;
    if (const CVarMetadata* cvar = EnginePtr->Console().Registry().FindCVar("editor.cook.cell_size");
        cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
        cellSize = std::get<double>(cvar->CurrentValue);

    if (!Assets)
    {
        log.Error("cook: asset system not initialized");
        return {};
    }

    const std::filesystem::path assetsRoot = std::filesystem::path(Project->Directory) / "assets";
    const LevelCookResult cooked =
        CookLevel(Workspace->Document, name, assetsRoot, cellSize, EnginePtr->Logging(), &*Assets);
    if (!cooked.Success)
    {
        log.Error("cook failed: " + cooked.Error);
        return {};
    }

    LastCookedMap = "levels/" + name;
    log.Info("cooked '{}' ({} cells) -> {}",
             LastCookedMap, cooked.CellCount, cooked.CookedScenePath.generic_string());
    return LastCookedMap;
}

void EditorApp::UnloadGameModule()
{
    if (!GameModule.IsValid())
        return;

    // Retract the serializers while the module is still mapped, then unmap.
    GameModule.Instance->OnUnregisterComponents(DefaultComponentSerializerRegistry());
    ModuleLoader.Unload(GameModule);
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

void EditorApp::StampOriginViewport(InputEvent& event)
{
    if (Router == nullptr || Workspace == nullptr)
        return;

    ViewportLayout& layout = Workspace->Layout;

    // While a gesture holds the pointer, every event belongs to the viewport it began
    // in; otherwise a positioned event belongs to the viewport under the cursor, which
    // also becomes the focused viewport. (Wheel carries no position and keeps
    // targeting the focused viewport downstream.)
    const auto resolve = [&](ImVec2 position) -> ViewportId
    {
        if (Router->PointerCaptured())
            return Router->CaptureViewport();
        const ViewportId hovered = layout.ResolveAt(position);
        if (hovered.IsValid())
            layout.SetActive(hovered);
        return hovered;
    };

    if (auto* e = std::get_if<PointerDownEvent>(&event))
        e->Viewport = resolve(e->Position);
    else if (auto* e = std::get_if<PointerUpEvent>(&event))
        e->Viewport = resolve(e->Position);
    else if (auto* e = std::get_if<PointerMoveEvent>(&event))
        e->Viewport = resolve(e->Position);
}
