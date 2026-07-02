#include "EditorServices.h"

#include "EditorFrameHook.h"
#include "viewport/EditorViewportCameraSystem.h"
#include "input/KeymapFile.h"
#include "input/SdlEventTranslation.h"
#include "input/UiInputGuard.h"
#include "commands/CompositeCommand.h"
#include "document/BrushBake.h"
#include "document/DocumentFileActions.h"
#include "document/DocumentSerialization.h"
#include "document/MaterialLibrary.h"
#include "document/commands/BakeBrushToMeshCommand.h"
#include "export/GltfMeshExport.h"
#include "project/PieDriver.h"
#include "render/EditorRenderFeature.h"
#include "ui/EditorConsolePanel.h"
#include "ui/EditorStatusBar.h"
#include "ui/EditorThemeFile.h"
#include "ui/EditorToolbar.h"
#include "ui/EditorUiFeature.h"
#include "ui/InspectorPanel.h"
#include "ui/MaterialPanel.h"
#include "ui/MeshEditPanel.h"
#include "ui/SceneHierarchyPanel.h"
#include "ui/ViewportPanel.h"

#include <SDL3/SDL.h>

#include "project/ProjectContentMount.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <assets/cook/AssetImporter.h>
#include <assets/cook/TextureCook.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
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

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#ifndef SENCHA_EDITOR_THEME_DIR
#define SENCHA_EDITOR_THEME_DIR "."
#endif

EditorServices::EditorServices(Engine& engine,
                               SdlWindow& window,
                               const EngineConfig& config,
                               std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
    EnginePtr = &engine;
    Window = &window;

    RegisterDocumentSerializers();
    // Load the project's game module (if any) BEFORE the document is created, so its
    // components are registered when the document's World registers storage.
    LoadGameModule();
    // Build the asset system and mount the project content (needs the project from
    // LoadGameModule). The document then serializes through it.
    InitAssets();
    BuildSourceWatch();

    BuildDocument();
    BuildPlayLoop();
    BuildFileActions();
    BuildInput();
    BuildViewportRendering();
    BuildUi(config.Console.OpenOnStart);
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
    // on teardown. The render feature's scene queues also hold StaticMeshCache handles +
    // material refs and tear down later (in ~Renderer), so release them here too.
    // Before the engine frees the graphics services the caches borrow.
    if (RenderFeature != nullptr)
        RenderFeature->ReleaseSceneResources();
    SourceWatch.reset();
    Assets.reset();
    // Toolbar, StatusBar, Materials, and the project/module state release with the
    // object in reverse declaration order; none touch the subsystems reset above.
}

void EditorServices::BuildDocument()
{
    Engine& engine = *EnginePtr;
    Commands = std::make_unique<CommandStack>();
    Workspace = std::make_unique<EditorWorkspace>(engine.Logging());
    if (Assets)
        Workspace->Document.SetAssetEnvironment(*Assets);
    Workspace->Layout.OnResize(Window->GetExtent().Width, Window->GetExtent().Height);
    Workspace->Init(*Commands);
}

void EditorServices::BuildPlayLoop()
{
    // The author -> cook -> play loop: cook the live document, launch/stop PIE, and
    // the cook/play/stop/project console commands all run through here.
    Engine& engine = *EnginePtr;
    Pie = std::make_unique<PieDriver>(engine, Workspace->Document,
                                      Project ? &*Project : nullptr,
                                      Assets ? &*Assets : nullptr);
    Pie->RegisterCommands(engine.Console().Registry());
}

void EditorServices::BuildFileActions()
{
    Engine& engine = *EnginePtr;
    Materials = std::make_unique<MaterialLibrary>(engine.Logging());
    std::vector<std::string> contentRoots;
    if (Project)
        contentRoots = Project->ContentRoots;
    // Populate the material list up front (not just after Open/SaveAs): with a
    // project the pickable set is the project's, independent of any level.
    if (!contentRoots.empty())
        Materials->Rescan(contentRoots);
    Files = std::make_unique<DocumentFileActions>(
        *Window, Workspace->Document, *Commands, Workspace->Selection, *Materials,
        std::move(contentRoots));
}

void EditorServices::BuildInput()
{
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

    // The editor keymap, as one table. Notes on the choices:
    // - Gizmo switches (Shift+Q/W/E/R) carry Shift to stay off the fly camera's
    //   bare W/A/S/D + Q/E; key events reach shortcuts even while the camera holds
    //   the pointer. The UI guard still blocks them while a text field is focused.
    // - Escape lands here only when no drag is in flight (the viewport dispatcher
    //   ahead in the chain consumes it to cancel an active interaction), so it
    //   climbs the editing context one level per press.
    struct KeyBinding
    {
        std::string_view Action;
        SDL_Keycode Key;
        ModifierFlags Mods;
        std::function<void()> Callback;
    };
    const KeyBinding bindings[] = {
        { "edit.undo",             SDLK_Z,      { .Ctrl = true },                [this] { Commands->Undo(); } },
        { "edit.redo",             SDLK_Z,      { .Ctrl = true, .Shift = true }, [this] { Commands->Redo(); } },
        { "edit.redo",             SDLK_Y,      { .Ctrl = true },                [this] { Commands->Redo(); } },
        { "edit.delete",           SDLK_DELETE, {},                              [this] { Workspace->DeleteSelection(); } },
        { "edit.select_all",       SDLK_A,      { .Ctrl = true },                [this] { Workspace->SelectAll(); } },
        { "edit.duplicate",        SDLK_D,      { .Ctrl = true },                [this] { Workspace->DuplicateSelection(/*asInstance*/ false); } },
        { "edit.duplicate_instance", SDLK_D,    { .Alt = true },                 [this] { Workspace->DuplicateSelection(/*asInstance*/ true); } },
        { "edit.escape",           SDLK_ESCAPE, {},                              [this] { Workspace->EscapeStep(); } },
        { "file.new",              SDLK_N,      { .Ctrl = true },                [this] { if (Files) Files->New(); } },
        { "file.open",             SDLK_O,      { .Ctrl = true },                [this] { if (Files) Files->RequestOpen(); } },
        { "file.save",             SDLK_S,      { .Ctrl = true },                [this] { if (Files) Files->Save(); } },
        { "mode.cycle",            SDLK_V,      { .Shift = true },               [this] { Workspace->MeshEdit.CycleElementKind(); } },
        { "mode.object",           SDLK_1,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Object); } },
        { "mode.vertex",           SDLK_2,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Vertex); } },
        { "mode.edge",             SDLK_3,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Edge); } },
        { "mode.face",             SDLK_4,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Face); } },
        { "gizmo.resize",          SDLK_Q,      { .Shift = true },               [this] { Workspace->Manipulators->SetTransformMode(TransformMode::Resize); } },
        { "gizmo.move",            SDLK_W,      { .Shift = true },               [this] { Workspace->Manipulators->SetTransformMode(TransformMode::Move); } },
        { "gizmo.rotate",          SDLK_E,      { .Shift = true },               [this] { Workspace->Manipulators->SetTransformMode(TransformMode::Rotate); } },
        { "gizmo.scale",           SDLK_R,      { .Shift = true },               [this] { Workspace->Manipulators->SetTransformMode(TransformMode::Scale); } },
        { "gizmo.space",           SDLK_T,      { .Shift = true },               [this] { Workspace->Manipulators->CycleTransformSpace(); } },
        { "grid.origin_selection", SDLK_G,      { .Shift = true },               [this] { Workspace->SetGridOriginToSelection(); } },
        { "grid.align_face",       SDLK_G,      { .Alt = true },                 [this] { Workspace->AlignGridToSelectedFace(); } },
        { "grid.reset",            SDLK_G,      { .Ctrl = true, .Shift = true }, [this] { Workspace->ResetGrid(); } },
        { "material.copy_proj",    SDLK_C,      { .Ctrl = true, .Shift = true }, [this] { if (MaterialsPanel) MaterialsPanel->CopyProjection(); } },
        { "material.paste_proj",   SDLK_V,      { .Ctrl = true, .Shift = true }, [this] { if (MaterialsPanel) MaterialsPanel->PasteProjection(); } },
    };
    // User keymap overrides ride on the action names: a keybinds.json in the
    // working directory rebinds any table entry without a recompile.
    std::string keymapError;
    const auto overrides = LoadKeymapOverrides("keybinds.json", &keymapError);
    if (!keymapError.empty())
        std::fprintf(stderr, "[editor] %s\n", keymapError.c_str());
    for (const KeyBinding& binding : bindings)
    {
        const auto it = overrides.find(std::string(binding.Action));
        if (it != overrides.end())
            Shortcuts->Register(binding.Action, it->second.Key, it->second.Mods, binding.Callback);
        else
            Shortcuts->Register(binding.Action, binding.Key, binding.Mods, binding.Callback);
    }

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
}

void EditorServices::BuildViewportRendering()
{
    Engine& engine = *EnginePtr;
    ConsoleService& console = engine.Console();

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

    // Grid look knobs, read per frame by EditorRenderFeature. Dial these live in the
    // dev console to tune the grid without recompiling.
    const auto registerGridFloat = [&](const char* name, double def, const char* help)
    {
        console.Registry().RegisterCVar({
            .Name = name,
            .Owner = "editor",
            .Type = CVarType::Double,
            .DefaultValue = def,
            .CurrentValue = def,
            .Flags = CVarFlags::Archive,
            .Help = help,
            .Source = { "editor" },
        });
    };
    registerGridFloat("editor.grid.cell_px", 3.0, "Editor grid: target on-screen cell size in px (density; larger = sparser).");
    registerGridFloat("editor.grid.opacity", 0.6, "Editor grid: line opacity 0..1 (larger = bolder).");
    registerGridFloat("editor.grid.brightness", 0.62, "Editor grid: line brightness 0..1 (gray level).");
    registerGridFloat("editor.grid.fade_start", -0.3, "Editor grid: fade start as a signed fraction of reach; negative fades gradually from near the camera (~ -0.3 is a good global falloff).");

    // Selection bloom/glow knobs, read per frame by EditorRenderFeature.
    console.Registry().RegisterCVar({
        .Name = "editor.bloom.enable",
        .Owner = "editor",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        .Flags = CVarFlags::Archive,
        .Help = "Editor: enable the selection bloom/glow pass.",
        .Source = { "editor" },
    });
    registerGridFloat("editor.bloom.threshold", 1.0, "Editor bloom: per-channel HDR threshold; only color above this glows.");
    registerGridFloat("editor.bloom.intensity", 1.0, "Editor bloom: additive strength of the glow.");
    registerGridFloat("editor.bloom.radius", 2.0, "Editor bloom: blur spread (larger = wider, softer glow).");

    // Hemispheric ambient (the no-bake indirect fill), read per frame by
    // EditorRenderFeature. Linear RGB: sky tint above, ground tint below.
    registerGridFloat("render.ambient.sky_r", 0.10, "Ambient sky tint (linear) red.");
    registerGridFloat("render.ambient.sky_g", 0.12, "Ambient sky tint (linear) green.");
    registerGridFloat("render.ambient.sky_b", 0.15, "Ambient sky tint (linear) blue.");
    registerGridFloat("render.ambient.ground_r", 0.04, "Ambient ground tint (linear) red.");
    registerGridFloat("render.ambient.ground_g", 0.03, "Ambient ground tint (linear) green.");
    registerGridFloat("render.ambient.ground_b", 0.02, "Ambient ground tint (linear) blue.");

    auto renderFeature = std::make_unique<EditorRenderFeature>(
        Workspace->Layout,
        Workspace->Document.GetScene(),
        Workspace->Selection,
        Workspace->MeshEdit,
        Workspace->Overlay,
        Workspace->Preview,
        *Workspace->Manipulators,
        Workspace->Grid,
        engine.Logging(),
        console.Registry(),
        Assets ? &Assets->Assets : nullptr,
        Assets ? &Assets->Registry : nullptr,
        Assets ? &*Assets : nullptr,
        Workspace->Document);
    RenderFeature = renderFeature.get();
    engine.Graphics().MainRenderer.AddFeature(std::move(renderFeature));
}

void EditorServices::BuildUi(bool consoleOpenOnStart)
{
    Engine& engine = *EnginePtr;
    ConsoleService& console = engine.Console();
    DebugService& debug = engine.Debug();

    // Chrome theme (directive: behavior from data). A theme name resolves under
    // the bundled themes/ directory; a path is used as-is. Empty keeps the
    // built-in palette. Loaded BEFORE the UI feature applies the ImGui style.
    console.Registry().RegisterCVar({
        .Name = "editor.ui.theme",
        .Owner = "editor",
        .Type = CVarType::String,
        .DefaultValue = std::string{},
        .CurrentValue = std::string{},
        .Flags = CVarFlags::Archive,
        .Help = "Editor chrome theme: a name under the bundled themes/ dir or a path to a theme JSON. Empty = built-in. Applied at startup.",
        .Source = { "editor" },
    });
    if (const CVarMetadata* themeVar = console.Registry().FindCVar("editor.ui.theme"))
    {
        if (const std::string* name = std::get_if<std::string>(&themeVar->CurrentValue);
            name != nullptr && !name->empty())
        {
            std::filesystem::path themePath(*name);
            std::error_code ec;
            if (!std::filesystem::exists(themePath, ec))
                themePath = std::filesystem::path(SENCHA_EDITOR_THEME_DIR) / (*name + ".json");
            std::string themeError;
            if (!LoadEditorTheme(themePath, &themeError) || !themeError.empty())
                std::fprintf(stderr, "[editor] %s\n", themeError.c_str());
        }
    }

    auto& instance = engine.Graphics().Instance;
    auto& frames = engine.Graphics().Frames;
    Renderer& renderer = engine.Graphics().MainRenderer;

    auto uiFeature = std::make_unique<EditorUiFeature>(engine, *Window, instance, frames);
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
                                              Workspace->BrushCreate, Workspace->EdgeCut);
    // The Cook/Play/Stop group routes through the same paths as the cook/play/stop
    // console commands.
    Toolbar->SetPlayControls({
        .Cook = [this] { if (Pie) Pie->Cook(""); },
        .Play = [this] { if (Pie) Pie->Play(Pie->LastCookedMap()); },
        .Stop = [this] { if (Pie) Pie->Stop(); },
        .IsPlaying = [this] { return Pie != nullptr && Pie->IsPlaying(); },
    });
    Toolbar->SetGridFrameControls({
        .OriginToSelection = [this] { Workspace->SetGridOriginToSelection(); },
        .AlignToFace = [this] { Workspace->AlignGridToSelectedFace(); },
        .RotateInPlane = [this] { Workspace->RotateGridInPlane(90.0f); },
        .Reset = [this] { Workspace->ResetGrid(); },
        .ToggleMoveOrigin = [this]
        { Workspace->Manipulators->SetEditingGridOrigin(!Workspace->Manipulators->IsEditingGridOrigin()); },
        .IsMovingOrigin = [this] { return Workspace->Manipulators->IsEditingGridOrigin(); },
    });
    Toolbar->SetTransformControls({
        .Session = Workspace->Manipulators,
        .SetOriginToPivot = [this] { Workspace->SetSelectedBrushOriginToPivot(); },
        .HasSelection = [this] { return !Workspace->Selection.GetSelection().empty(); },
    });
    StatusBar = std::make_unique<EditorStatusBar>(
        *Workspace->Tools, Workspace->Layout, Workspace->Selection, Workspace->Grid,
        Workspace->MeshEdit, *Workspace->Manipulators);
    ToolSidebar = std::make_unique<EditorToolSidebar>(*Workspace->Tools);
    UiFeature->AddChrome([this] { Toolbar->Draw(); });
    UiFeature->AddChrome([this] { StatusBar->Draw(); });
    UiFeature->AddChrome([this] { ToolSidebar->Draw(); });

    auto viewportPanel = std::make_unique<ViewportPanel>(Workspace->Layout, Workspace->Marquee, Workspace->Overlay,
                                                         RenderFeature->GetViewportTargets());
    Viewports = viewportPanel.get();
    UiFeature->AddPanel(std::move(viewportPanel));
    auto editorConsole = std::make_unique<EditorConsolePanel>(debug.GetLogSink(), console);
    ConsolePanel = editorConsole.get();
    ConsolePanel->SetVisible(consoleOpenOnStart);
    UiFeature->AddPanel(std::move(editorConsole));
    UiFeature->AddPanel(std::make_unique<SceneHierarchyPanel>(
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<InspectorPanel>(
        Workspace->Document.GetScene(), Workspace->Document, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<MeshEditPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands,
        MeshEditPanel::ObjectActions{
            .Duplicate = [this] { Workspace->DuplicateSelection(/*asInstance*/ false); },
            .Instance = [this] { Workspace->DuplicateSelection(/*asInstance*/ true); },
            .MakeUnique = [this] { Workspace->MakeSelectedBrushesUnique(); },
            .Merge = [this] { Workspace->MergeSelectedBrushes(); },
            .SeparateFaces = [this] { Workspace->SeparateSelectedFaces(); },
            .Bake = [this] { BakeSelectedBrushes(); },
            .Revert = [this] { RevertSelectedBakedBrushes(); },
            .ExportGlb = [this] { ExportSelectionGlb(); },
            .HasBakedSelection = [this] { return SelectionHasBakedBrush(); },
            .HasInstancedSelection = [this]
            {
                const EditorScene& scene = Workspace->Document.GetScene();
                for (const SelectableRef& ref : Workspace->Selection.GetSelection())
                    if (ref.IsEntity() && scene.IsBrushInstanced(ref.Entity))
                        return true;
                return false;
            },
        }));
    auto materialPanel = std::make_unique<MaterialPanel>(
        *Workspace->Sink, Workspace->Selection, Workspace->MeshEdit, *Commands,
        *Materials, Workspace->Document);
    MaterialsPanel = materialPanel.get();
    UiFeature->AddPanel(std::move(materialPanel));

    renderer.AddFeature(std::move(uiFeature));
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

//=============================================================================
// Source hot reload: AssetSourceWatcher detects content changes to authored
// .smat/.png under each content root; AssetHotReloader re-cooks (textures) or
// re-parses (materials) and swaps the resident cache slot in place at the
// engine's async drain point. Live handles never change, so the viewport just
// shows the new data on its next frame.
//=============================================================================
struct EditorServices::SourceWatchState
{
    struct RootWatch
    {
        AssetSourceWatcher Watcher;
        AssetHotReloader Reloader;
    };

    PngTextureImporter TextureImporter;
    AssetImporterRegistry Importers;
    std::vector<std::unique_ptr<RootWatch>> Roots;
    std::chrono::steady_clock::time_point NextPoll{};
};

void EditorServices::BuildSourceWatch()
{
    if (!Project || !Assets || Project->ContentRoots.empty())
        return;

    Engine& engine = *EnginePtr;
    SourceWatch = std::make_unique<SourceWatchState>();
    SourceWatch->Importers.Register(SourceWatch->TextureImporter);
    for (const std::string& root : Project->ContentRoots)
    {
        auto watch = std::unique_ptr<SourceWatchState::RootWatch>(new SourceWatchState::RootWatch{
            AssetSourceWatcher(engine.Logging(), root, { ".smat", ".png" }),
            AssetHotReloader(engine.Logging(), Assets->Assets, Assets->Registry,
                             SourceWatch->Importers, engine.Tasks(), root),
        });
        watch->Watcher.Initialize();
        SourceWatch->Roots.push_back(std::move(watch));
    }
}

void EditorServices::ProcessFrame()
{
    if (Files)
    {
        Files->ProcessPending();
        Files->UpdateTitle();
    }

    // Poll watched sources on an interval, not per frame: the watcher is a
    // content-hash-confirmed mtime scan over the content roots. A save from
    // the material editor or a text editor lands in the viewport within ~0.5s.
    // Files created after startup are not watched (Decision H); the material
    // panel's Rescan refreshes the pickable list for those.
    if (SourceWatch)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= SourceWatch->NextPoll)
        {
            SourceWatch->NextPoll = now + std::chrono::milliseconds(500);
            for (auto& root : SourceWatch->Roots)
                for (const std::string& changed : root->Watcher.PollChanged())
                    root->Reloader.ReloadSource(changed);
        }
    }

    // Rebuild the transient viewport overlay (selected-brush dimension labels)
    // before the UI panel draws it this frame, and keep the ortho views aligned
    // to the (possibly gizmo-dragged) grid frame.
    if (Workspace)
    {
        Workspace->UpdateOverlay();
        Workspace->SyncOrthoViewsToGridFrame();
    }
}

namespace
{
// Self-contained export payload handed to the async save dialog: the callback
// owns it and touches no editor state.
struct GlbExportPayload
{
    MeshGeometry Geometry;
    std::vector<AssetRef> Materials;
};
}

void EditorServices::BakeSelectedBrushes()
{
    if (!Assets || !Project || Project->ContentRoots.empty())
    {
        std::fprintf(stderr, "[editor] bake needs an open project (SENCHA_PROJECT) with a content root\n");
        return;
    }

    Engine& engine = *EnginePtr;
    const std::filesystem::path contentRoot(Project->ContentRoots.front());
    EditorScene& scene = Workspace->Document.GetScene();

    // Snapshot the entity list first: executing a command must not invalidate the
    // selection span being walked.
    std::vector<EntityId> targets;
    for (const SelectableRef& ref : Workspace->Selection.GetSelection())
        if (ref.IsEntity() && scene.TryGetBrush(ref.Entity) != nullptr)
            targets.push_back(ref.Entity);

    std::vector<std::unique_ptr<ICommand>> commands;
    for (EntityId entity : targets)
        if (auto command = MakeBakeBrushToMeshCommand(scene, Workspace->Document, Assets->Assets,
                                                      Assets->Registry, engine.Logging(),
                                                      entity, contentRoot))
            commands.push_back(std::move(command));

    if (commands.empty())
        return;
    if (commands.size() == 1)
        Commands->Execute(std::move(commands.front()));
    else
        Commands->Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void EditorServices::RevertSelectedBakedBrushes()
{
    if (!Assets)
        return;

    EditorScene& scene = Workspace->Document.GetScene();
    std::vector<EntityId> targets;
    for (const SelectableRef& ref : Workspace->Selection.GetSelection())
        if (ref.IsEntity() && scene.TryGetBakedBrush(ref.Entity) != nullptr)
            targets.push_back(ref.Entity);

    std::vector<std::unique_ptr<ICommand>> commands;
    for (EntityId entity : targets)
        if (auto command = MakeRevertBakedBrushCommand(scene, Workspace->Document, Assets->Assets, entity))
            commands.push_back(std::move(command));

    if (commands.empty())
        return;
    if (commands.size() == 1)
        Commands->Execute(std::move(commands.front()));
    else
        Commands->Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

bool EditorServices::SelectionHasBakedBrush() const
{
    if (!Workspace)
        return false;
    const EditorScene& scene = Workspace->Document.GetScene();
    for (const SelectableRef& ref : Workspace->Selection.GetSelection())
        if (ref.IsEntity() && scene.TryGetBakedBrush(ref.Entity) != nullptr)
            return true;
    return false;
}

void EditorServices::ExportSelectionGlb()
{
    if (Window == nullptr || Window->GetHandle() == nullptr)
        return;

    const EditorScene& scene = Workspace->Document.GetScene();

    // First selected entity with a live or dormant brush mesh supplies the
    // geometry, baked in local space through the same kernel as the level cook.
    const BrushMesh* mesh = nullptr;
    for (const SelectableRef& ref : Workspace->Selection.GetSelection())
    {
        if (!ref.IsEntity())
            continue;
        mesh = scene.TryGetBrushMesh(ref.Entity);
        if (mesh == nullptr)
            mesh = scene.TryGetDormantBrushMesh(ref.Entity);
        if (mesh != nullptr)
            break;
    }
    if (mesh == nullptr)
    {
        std::fprintf(stderr, "[editor] export: select a brush or baked brush first\n");
        return;
    }

    auto payload = std::make_unique<GlbExportPayload>();
    std::string error;
    if (!BakeBrushToGeometry(*mesh, Workspace->Document.GetDefaultMaterial(),
                             payload->Geometry, payload->Materials, &error))
    {
        std::fprintf(stderr, "[editor] export: %s\n", error.c_str());
        return;
    }

    static constexpr SDL_DialogFileFilter kGlbFilters[] = { { "Binary glTF", "glb" } };
    SDL_ShowSaveFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            // The dialog callback may run off the main thread; the payload is
            // self-contained (no editor state), so writing here is safe.
            std::unique_ptr<GlbExportPayload> payload(static_cast<GlbExportPayload*>(userdata));
            if (filelist == nullptr || filelist[0] == nullptr)
                return;
            std::filesystem::path path(filelist[0]);
            if (path.extension() != ".glb")
                path += ".glb";
            std::string writeError;
            if (!WriteGlbFile(payload->Geometry, payload->Materials, path, &writeError))
                std::fprintf(stderr, "[editor] export: %s\n", writeError.c_str());
            else
                std::fprintf(stderr, "[editor] exported '%s'\n", path.string().c_str());
        },
        payload.release(),
        Window->GetHandle(),
        kGlbFilters,
        static_cast<int>(std::size(kGlbFilters)),
        nullptr);
}

void EditorServices::LoadGameModule()
{
    // Prefer a project descriptor (--project / SENCHA_PROJECT, resolved by the
    // caller); fall back to a bare module path (SENCHA_GAME_MODULE) so the
    // pre-project workflow still works.
    std::string modulePath;
    if (ProjectPath)
    {
        ProjectDescriptor descriptor;
        std::string error;
        if (!ProjectDescriptor::Load(*ProjectPath, descriptor, &error))
        {
            std::fprintf(stderr, "[editor] failed to open project '%s': %s\n",
                         ProjectPath->c_str(), error.c_str());
            return;
        }
        Project = std::move(descriptor);
        modulePath = Project->GameModulePath;
        std::fprintf(stderr, "[editor] opened project '%s' (%s)\n",
                     Project->Name.c_str(), ProjectPath->c_str());
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

    MountProjectContent(*Project, *Assets, logging);
}

void EditorServices::UnloadGameModule()
{
    if (!GameModule.IsValid())
        return;

    // Retract the serializers while the module is still mapped, then unmap.
    GameModule.Instance->OnUnregisterComponents(DefaultComponentSerializerRegistry());
    ModuleLoader.Unload(GameModule);
}
