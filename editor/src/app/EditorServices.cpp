#include "EditorServices.h"

#include "EditorFrameHook.h"
#include "../viewport/EditorViewportCameraSystem.h"
#include "../input/SdlEventTranslation.h"
#include "../input/UiInputGuard.h"
#include "../level/DocumentFileActions.h"
#include "../level/LevelSerialization.h"
#include "../level/MaterialLibrary.h"
#include "../project/PieDriver.h"
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
#include <app/EngineSchedule.h>
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
#include <platform/SdlWindow.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

EditorServices::EditorServices(Engine& engine, SdlWindow& window, const EngineConfig& config)
{
    EnginePtr = &engine;
    Window = &window;

    auto& console = engine.Console();
    auto& debug = engine.Debug();
    auto& instance = engine.Graphics().Instance;
    auto& frames = engine.Graphics().Frames;
    auto& renderer = engine.Graphics().MainRenderer;

    RegisterLevelSerializers();

    // Load the project's game module (if any) BEFORE the document is created, so
    // its components are registered when the document's World registers storage.
    LoadGameModule();

    // Build the asset system and mount the project content (needs the project from
    // LoadGameModule). The document then serializes through it.
    InitAssets();

    Commands = std::make_unique<CommandStack>();
    Workspace = std::make_unique<LevelWorkspace>(engine.Logging());
    if (Assets)
        Workspace->Document.SetAssetEnvironment(*Assets);
    Workspace->Layout.OnResize(window.GetExtent().Width, window.GetExtent().Height);
    Workspace->Init(*Commands);

    // The author -> cook -> play loop: cook the live document, launch/stop PIE,
    // and the cook/play/stop/project console commands all run through here.
    Pie = std::make_unique<PieDriver>(engine, Workspace->Document,
                                      Project ? &*Project : nullptr,
                                      Assets ? &*Assets : nullptr);
    Pie->RegisterCommands(console.Registry());

    Materials = std::make_unique<MaterialLibrary>(engine.Logging());
    Files = std::make_unique<DocumentFileActions>(
        window, Workspace->Document, *Commands, Workspace->Selection, *Materials);

    Navigation = std::make_unique<ViewportNavigation>(
        Workspace->Layout,
        [this](bool enabled)
        {
            // Fly-look only: hide the cursor and switch to relative mouse. The ImGui
            // mouse gate is driven by pointer capture (Router->SetCaptureChanged
            // below), which also covers ortho-pan and tool drags.
            if (Window != nullptr)
                SetRelativeMouseMode(*Window, enabled);
        });

    Shortcuts = std::make_unique<ShortcutRegistry>();
    Shortcuts->Register(SDLK_Z, { .Ctrl = true }, [this] { Commands->Undo(); });
    Shortcuts->Register(SDLK_Z, { .Ctrl = true, .Shift = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_Y, { .Ctrl = true }, [this] { Commands->Redo(); });
    Shortcuts->Register(SDLK_DELETE, {}, [this] { Workspace->DeleteSelection(); });
    Shortcuts->Register(SDLK_N, { .Ctrl = true }, [this] { if (Files) Files->New(); });
    Shortcuts->Register(SDLK_O, { .Ctrl = true }, [this] { if (Files) Files->RequestOpen(); });
    Shortcuts->Register(SDLK_S, { .Ctrl = true }, [this] { if (Files) Files->Save(); });
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

    // The solid pass reads this cvar to backface-cull the editor viewport to match
    // play mode (EditorRenderFeature / EditorSolidPipeline).
    console.Registry().RegisterCVar({
        .Name = "editor.cull_backfaces",
        .Owner = "editor",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        .Flags = CVarFlags::Archive,
        .Help = "Backface-cull the editor solid viewport to match play mode.",
        .Source = { "editor" },
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

    auto uiFeature = std::make_unique<EditorUiFeature>(engine, window, instance, frames);
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]() { if (Commands) Commands->Undo(); },
        [this]() { if (Commands) Commands->Redo(); },
        [this]() { return Commands != nullptr && Commands->CanUndo(); },
        [this]() { return Commands != nullptr && Commands->CanRedo(); });
    UiFeature->SetFileActions(
        [this]() { if (Files) Files->New(); },
        [this]() { if (Files) Files->RequestOpen(); },
        [this]() { if (Files) Files->Save(); },
        [this]() { if (Files) Files->RequestSaveAs(); });

    // Fixed app chrome: top toolbar + bottom status bar. Registered before the
    // panels so the work-area space they reserve is subtracted from the full-bleed
    // viewport panel below.
    Toolbar = std::make_unique<EditorToolbar>(*Workspace->Tools, Workspace->MeshEdit, Workspace->Grid,
                                              Workspace->BrushCreate);
    // The Cook/Play/Stop group routes through the same paths as the cook/play/stop
    // console commands.
    Toolbar->SetPlayControls({
        .Cook = [this] { if (Pie) Pie->Cook(""); },
        .Play = [this] { if (Pie) Pie->Play(Pie->LastCookedMap()); },
        .Stop = [this] { if (Pie) Pie->Stop(); },
        .IsPlaying = [this] { return Pie != nullptr && Pie->IsPlaying(); },
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
    ConsolePanel->SetVisible(config.Console.OpenOnStart);
    UiFeature->AddPanel(std::move(editorConsole));
    UiFeature->AddPanel(std::make_unique<ToolPalettePanel>(*Workspace->Tools));
    UiFeature->AddPanel(std::make_unique<SceneHierarchyPanel>(
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<InspectorPanel>(
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<MeshEditPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands));
    UiFeature->AddPanel(std::make_unique<MaterialPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands,
        *Materials, Workspace->Document));

    renderer.AddFeature(std::move(uiFeature));
}

EditorServices::~EditorServices()
{
    if (Window != nullptr)
        SetRelativeMouseMode(*Window, false);

    // Pie and Files reference Workspace/Commands/Materials/Project; tear them down
    // before that state goes away.
    Files.reset();
    Pie.reset();
    UnloadGameModule();
    Workspace.reset();
    Commands.reset();
    Router.reset();
    Navigation.reset();
    Shortcuts.reset();
    // After Workspace: the document's StaticMeshComponents release into these caches
    // on teardown. Before the engine frees the graphics services the caches borrow.
    Assets.reset();
    // Toolbar, StatusBar, Materials, and the project/module state release with the
    // object in reverse declaration order; none touch the subsystems reset above.
}

void EditorServices::RegisterSystems(EngineSchedule& schedule)
{
    CameraSystem = &schedule.Register<EditorViewportCameraSystem>(Workspace->Layout);
    FrameHook = &schedule.Register<EditorFrameHook>([this] { ProcessFrame(); });
}

void EditorServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    switch (ctx.Event.type)
    {
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
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
        // per-device special-casing here. Pointer events are stamped with their
        // origin viewport first, so navigation and tools never re-resolve it.
        std::optional<InputEvent> event = TranslateSdlEvent(ctx.Event);
        if (event.has_value())
        {
            StampOriginViewport(*Router, Workspace->Layout, *event);
            if (Router->Route(*event) == InputConsumed::Yes)
                ctx.Handled = true;
        }
    }
}

void EditorServices::ProcessFrame()
{
    if (Files)
    {
        Files->ProcessPending();
        Files->UpdateTitle();
    }
}

void EditorServices::LoadGameModule()
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

void EditorServices::InitAssets()
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
    Logger& log = logging.GetLogger<EditorServices>();
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

void EditorServices::UnloadGameModule()
{
    if (!GameModule.IsValid())
        return;

    // Retract the serializers while the module is still mapped, then unmap.
    GameModule.Instance->OnUnregisterComponents(DefaultComponentSerializerRegistry());
    ModuleLoader.Unload(GameModule);
}
