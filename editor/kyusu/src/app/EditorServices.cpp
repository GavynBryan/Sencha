#include "EditorServices.h"

#include "EditorFrameHook.h"
#include "viewport/EditorViewportCameraSystem.h"
#include "editmodes/ManipulatorSession.h"
#include "workspace/BrushManipulationSink.h"
#include "input/KeymapFile.h"
#include "tools/ToolRegistry.h"
#include "input/ViewportToolDispatcher.h"
#include "input/SdlEventTranslation.h"
#include "input/UiInputGuard.h"
#include "commands/CompositeCommand.h"
#include "document/BrushBake.h"
#include "document/DocumentFileActions.h"
#include "document/DocumentSerialization.h"
#include "document/LightingReadModel.h"
#include "EditorCookRuntime.h"
#include "project/MaterialLibrary.h"
#include "document/commands/BakeBrushToMeshCommand.h"
#include "export/GltfMeshExport.h"
#include "render/EditorRenderFeature.h"
#include "ui/ActiveMaterialPanel.h"
#include "ui/CookProfilesPanel.h"
#include "ui/EditorConsolePanel.h"
#include "ui/EditorStatusBar.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorToolbar.h"
#include "ui/EditorUiFeature.h"
#include "ui/InspectorPanel.h"
#include "ui/LightingPanel.h"
#include "ui/MaterialBrowserPanel.h"
#include "ui/MaterialThumbnailCache.h"
#include "ui/ToolPropertiesPanel.h"
#include "ui/SceneHierarchyPanel.h"
#include "ui/WorldPartitionPanel.h"
#include "ui/GraphViewerPanel.h"
#include "ui/ViewportPanel.h"

#include <SDL3/SDL.h>

#include "project/ProjectContentMount.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <assets/cook/AssetImporter.h> // importer registry + kImportSettingsSuffix
#include <assets/cook/TextureCook.h>
#include <render/LightComponentTypes.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
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

    // The cook runtime and Files reference Workspace/Commands/Materials/Project;
    // tear them down before that state goes away.
    Files.reset();
    CookRuntime.reset();
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
    // The thumbnail bindings release texture refs through Assets and free ImGui
    // descriptor sets, so this must land after the render feature's release and
    // before Assets goes away (the panels referencing the cache never touch it
    // in their destructors).
    Thumbnails.reset();
    SourceWatch.reset();
    Assets.reset();
    // Toolbar, StatusBar, Materials, and the project/module state release with the
    // object in reverse declaration order; none touch the subsystems reset above.
}

void EditorServices::BuildDocument()
{
    Engine& engine = *EnginePtr;
    Commands = std::make_unique<CommandStack>();
    Workspace = std::make_unique<EditorWorkspace>(engine.Logging(), *Commands);
    if (Assets)
        Workspace->World.SetAssetEnvironment(*Assets);
    Workspace->Layout.OnResize(Window->GetExtent().Width, Window->GetExtent().Height);
}

void EditorServices::BuildPlayLoop()
{
    Engine& engine = *EnginePtr;
    CookRuntime = std::make_unique<EditorCookRuntime>(engine, Workspace->World,
                                                      Project ? &*Project : nullptr,
                                                      Assets ? &*Assets : nullptr);
    CookRuntime->RegisterConsoleCommands(engine.Console().Registry());
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
    // Baking writes a mesh asset into the project, so the selection actions get
    // the asset environment once it exists (they stay inert without one).
    if (Assets && Project && !Project->ContentRoots.empty())
        Workspace->Actions.SetAssetEnvironment(*Assets, Project->ContentRoots.front(),
                                               engine.Logging());
    Files = std::make_unique<DocumentFileActions>(
        *Window, Workspace->World, [this] { Workspace->ResolvePendingEdits(); },
        *Materials, std::move(contentRoots));
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
        { "edit.dissolve",         SDLK_BACKSPACE, {},                           [this] { Workspace->DissolveSelectedEdges(); } },
        { "edit.select_all",       SDLK_A,      { .Ctrl = true },                [this] { Workspace->SelectAll(); } },
        { "edit.duplicate",        SDLK_D,      { .Ctrl = true },                [this] { Workspace->Actions.Duplicate(/*asInstance*/ false); } },
        { "edit.duplicate_instance", SDLK_D,    { .Alt = true },                 [this] { Workspace->Actions.Duplicate(/*asInstance*/ true); } },
        { "edit.repeat",           SDLK_R,      { .Ctrl = true },                [this] { Workspace->Actions.RepeatLast(); } },
        { "edit.escape",           SDLK_ESCAPE, {},                              [this] { Workspace->EscapeStep(); } },
        { "file.new",              SDLK_N,      { .Ctrl = true },                [this] { if (Files) Files->New(); } },
        { "file.open",             SDLK_O,      { .Ctrl = true },                [this] { if (Files) Files->RequestOpen(); } },
        { "file.save",             SDLK_S,      { .Ctrl = true },                [this] { if (Files) Files->Save(); } },
        { "mode.cycle",            SDLK_V,      { .Shift = true },               [this] { Workspace->MeshEdit.CycleElementKind(); } },
        { "mode.object",           SDLK_1,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Object); } },
        { "mode.vertex",           SDLK_2,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Vertex); } },
        { "mode.edge",             SDLK_3,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Edge); } },
        { "mode.face",             SDLK_4,      {},                              [this] { Workspace->MeshEdit.SetElementKind(MeshElementKind::Face); } },
        { "gizmo.resize",          SDLK_Q,      { .Shift = true },               [this] { Workspace->Interaction.Manipulators->SetTransformMode(TransformMode::Resize); } },
        { "gizmo.move",            SDLK_W,      { .Shift = true },               [this] { Workspace->Interaction.Manipulators->SetTransformMode(TransformMode::Move); } },
        { "gizmo.rotate",          SDLK_E,      { .Shift = true },               [this] { Workspace->Interaction.Manipulators->SetTransformMode(TransformMode::Rotate); } },
        { "gizmo.scale",           SDLK_R,      { .Shift = true },               [this] { Workspace->Interaction.Manipulators->SetTransformMode(TransformMode::Scale); } },
        { "gizmo.space",           SDLK_G,      { .Ctrl = true },                [this] { Workspace->Interaction.Manipulators->CycleTransformSpace(); } },
        { "grid.origin_selection", SDLK_G,      { .Shift = true },               [this] { Workspace->SetGridOriginToSelection(); } },
        { "grid.align_face",       SDLK_G,      { .Alt = true },                 [this] { Workspace->AlignGridToSelectedFace(); } },
        { "grid.reset",            SDLK_G,      { .Ctrl = true, .Shift = true }, [this] { Workspace->ResetGrid(); } },
        { "grid.finer",            SDLK_LEFTBRACKET,  {},                        [this] { Workspace->Grid.StepSpacing(-1); } },
        { "grid.coarser",          SDLK_RIGHTBRACKET, {},                        [this] { Workspace->Grid.StepSpacing(+1); } },
        { "material.apply",        SDLK_T,      { .Shift = true },               [this] { Workspace->ApplyActiveMaterialToSelectedFaces(); } },
        { "material.copy_proj",    SDLK_C,      { .Ctrl = true, .Shift = true }, [this] { Workspace->CopySelectedFaceProjection(); } },
        { "material.paste_proj",   SDLK_V,      { .Ctrl = true, .Shift = true }, [this] { Workspace->PasteFaceProjectionToSelection(); } },
    };
    // User keymap overrides ride on the action names: a keybinds.json in the
    // working directory rebinds any table entry without a recompile.
    std::string keymapError;
    const auto overrides = LoadKeymapOverrides("keybinds.json", &keymapError);
    if (!keymapError.empty())
        std::fprintf(stderr, "[editor] %s\n", keymapError.c_str());
    const auto registerBinding = [&](std::string_view action, SDL_Keycode key,
                                     ModifierFlags mods, std::function<void()> callback)
    {
        const auto it = overrides.find(std::string(action));
        if (it != overrides.end())
            Shortcuts->Register(action, it->second.Key, it->second.Mods, std::move(callback));
        else if (key != SDLK_UNKNOWN)
            Shortcuts->Register(action, key, mods, std::move(callback));
    };

    for (const KeyBinding& binding : bindings)
        registerBinding(binding.Action, binding.Key, binding.Mods, binding.Callback);

    // Tool activation is generated from the registry rather than listed above: a
    // tool declares its own key, so adding one needs no edit here. The action
    // name is "tool.<id>", which is also what a keymap file overrides, and the
    // registry resolves at fire time because the workspace owns it.
    if (ToolRegistry* tools = Workspace->Interaction.Tools.get())
    {
        for (const std::unique_ptr<ITool>& tool : tools->GetTools())
        {
            if (tool == nullptr)
                continue;
            const std::string id(tool->GetId());
            const ITool::Shortcut shortcut = tool->GetShortcut();
            registerBinding("tool." + id, shortcut.Key, shortcut.Mods,
                            [this, id]
                            {
                                if (ToolRegistry* live = Workspace->Interaction.Tools.get())
                                    (void)live->Activate(id);
                            });
        }
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
            const bool overViewport =
                (PerspectivePanel != nullptr && PerspectivePanel->IsViewportRegionHovered())
                || (OrthoPanel != nullptr && OrthoPanel->IsViewportRegionHovered());
            if (overViewport)
                capture.Mouse = false;
            return capture;
        }));
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Navigation->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Workspace->Interaction.Dispatcher->OnInput(e, cap); });
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

    // The render.ambient.* cvars EditorRenderFeature polls are the engine's
    // own registrations (EngineConsoleBuiltins); the editor registers nothing
    // for them.

    auto renderFeature = std::make_unique<EditorRenderFeature>(
        Workspace->Layout,
        Workspace->World,
        *Workspace->Affordances,
        Workspace->Selection,
        Workspace->MeshEdit,
        Workspace->Interaction.Overlay,
        Workspace->Interaction.Preview,
        [this]() -> const ManipulatorSession* { return Workspace->Interaction.Manipulators; },
        Workspace->Grid,
        Workspace->WorldView,
        engine.Logging(),
        console.Registry(),
        Assets ? &Assets->Assets : nullptr,
        Assets ? &Assets->Registry : nullptr,
        Assets ? &*Assets : nullptr);
    RenderFeature = renderFeature.get();
    engine.Graphics().MainRenderer.AddFeature(std::move(renderFeature));
}

void EditorServices::BuildUi(bool consoleOpenOnStart)
{
    Engine& engine = *EnginePtr;
    ConsoleService& console = engine.Console();
    DebugService& debug = engine.Debug();

    // Chrome theme (directive: behavior from data), loaded BEFORE the UI
    // feature applies the ImGui style.
    ApplyEditorThemeFromConsole(console);

    auto& instance = engine.Graphics().Instance;
    auto& frames = engine.Graphics().Frames;
    Renderer& renderer = engine.Graphics().MainRenderer;

    // Default layout proportions: mesh tools over the active material in a
    // narrow left column, the perspective viewport dominating the center with
    // the ortho view + Materials/Console strip under it, world/hierarchy row
    // over the inspector on the right.
    const DockLayoutRatios layoutRatios{
        .Left = 0.15f,
        .Right = 0.3f,
        .CenterBottom = 0.35f,
        .RightBottom = 0.3f,
    };
    auto uiFeature = std::make_unique<EditorUiFeature>(engine, *Window, instance, frames,
                                                       "kyusu.imgui.ini", layoutRatios);
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
    UiFeature->SetNewWorldAction([this]() { if (Files) Files->NewWorld(); });

    // Fixed app chrome: top toolbar + bottom status bar. Registered before the
    // panels so the work-area space they reserve is subtracted from the full-bleed
    // viewport panel below.
    Toolbar = std::make_unique<EditorToolbar>(
        [this] { return Workspace->Interaction.Tools.get(); },
        [this] { return Workspace->Interaction.Manipulators; },
        Workspace->MeshEdit, Workspace->Grid, Workspace->WorldView);
    // The Cook/Play/Stop group routes through the same paths as the cook/play/stop
    // console commands.
    // A cook reads the live documents (and force-saves the world first), so open
    // previews settle before it starts or they would cook half-staged.
    Toolbar->SetPlayControls({
        .RunCook = [this] {
            Workspace->ResolvePendingEdits();
            if (CookRuntime) CookRuntime->Start();
        },
        .CancelCook = [this] { if (CookRuntime) CookRuntime->Cancel(); },
        .RebuildCook = [this] {
            Workspace->ResolvePendingEdits();
            if (CookRuntime) CookRuntime->Start(/*rebuild*/ true);
        },
        .IsCooking = [this] { return CookRuntime != nullptr && CookRuntime->IsActive(); },
        .Profiles = [this] {
            std::vector<EditorToolbar::PlayControls::ProfileChoice> choices;
            if (CookRuntime)
                for (const CookProfile& profile : CookRuntime->GetSession().AvailableProfiles())
                    choices.push_back({ profile.Id, profile.Name, profile.BuiltIn });
            return choices;
        },
        .SelectedProfileId = [this] { return CookRuntime ? CookRuntime->SelectedProfileId() : std::string{}; },
        .SelectProfile = [this](std::string_view id) {
            if (CookRuntime) CookRuntime->SelectProfile(std::string(id));
        },
        .OpenProfiles = [this] {
            if (CookRuntime && CookRuntime->ProfilesPanel() != nullptr)
                CookRuntime->ProfilesPanel()->SetVisible(true);
        },
        .CookStatus = [this] {
            if (!CookRuntime)
                return std::string{};
            const CookSession::Status status = CookRuntime->GetSession().GetStatus();
            if (!status.Active)
                return status.LastError;
            std::string text = status.ProfileName;
            if (!status.StepName.empty())
                text += ": " + status.StepName;
            if (status.TotalSteps > 0)
                text += " (" + std::to_string(status.CompletedSteps) + "/" +
                        std::to_string(status.TotalSteps) + ")";
            return text;
        },
        .Play = [this] { if (CookRuntime) CookRuntime->Player().Play(CookRuntime->Player().LastCookedMap()); },
        .Stop = [this] { if (CookRuntime) CookRuntime->Player().Stop(); },
        .IsPlaying = [this] { return CookRuntime != nullptr && CookRuntime->Player().IsPlaying(); },
    });
    Toolbar->SetGridFrameControls({
        .OriginToSelection = [this] { Workspace->SetGridOriginToSelection(); },
        .AlignToFace = [this] { Workspace->AlignGridToSelectedFace(); },
        .RotateInPlane = [this] { Workspace->RotateGridInPlane(90.0f); },
        .Reset = [this] { Workspace->ResetGrid(); },
        .ToggleMoveOrigin = [this]
        { Workspace->Interaction.Manipulators->SetEditingGridOrigin(!Workspace->Interaction.Manipulators->IsEditingGridOrigin()); },
        .IsMovingOrigin = [this] { return Workspace->Interaction.Manipulators->IsEditingGridOrigin(); },
    });
    Toolbar->SetTransformControls({
        .SetOriginToPivot = [this] { Workspace->SetSelectedBrushOriginToPivot(); },
        .SetOriginToVertex = [this]
        { Workspace->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::SelectedVertex); },
        .SetOriginToBoundsCenter = [this]
        { Workspace->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::BoundsCenter); },
        .SetOriginToBoundsCorner = [this]
        { Workspace->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::BoundsMinCorner); },
        .HasSelection = [this] { return !Workspace->Selection.GetSelection().empty(); },
    });
    StatusBar = std::make_unique<EditorStatusBar>(
        [this] { return Workspace->Interaction.Tools.get(); },
        [this]() -> const ManipulatorSession* { return Workspace->Interaction.Manipulators; },
        Workspace->Layout, Workspace->Selection, Workspace->Grid,
        Workspace->MeshEdit);
    ToolSidebar = std::make_unique<EditorToolSidebar>([this] { return Workspace->Interaction.Tools.get(); });
    UiFeature->AddChrome([this] { Toolbar->Draw(); });
    UiFeature->AddChrome([this] { StatusBar->Draw(); });
    UiFeature->AddChrome([this] { ToolSidebar->Draw(); });

    // One panel per viewport: the perspective view owns the central node, the
    // ortho view shares the center-bottom strip with the Materials browser.
    ViewportId perspectiveId{};
    ViewportId orthoId{};
    for (const auto& viewport : Workspace->Layout.All())
    {
        if (viewport == nullptr)
            continue;
        if (viewport->Orientation == ViewportOrientation::Perspective)
            perspectiveId = viewport->Id;
        else
            orthoId = viewport->Id;
    }
    auto perspectivePanel = std::make_unique<ViewportPanel>(
        Workspace->Layout, Workspace->Interaction.Marquee, Workspace->Interaction.Overlay,
        RenderFeature->GetViewportTargets(), "Viewport", DockSlot::Center, 1.0f, perspectiveId);
    PerspectivePanel = perspectivePanel.get();
    UiFeature->AddPanel(std::move(perspectivePanel));
    auto orthoPanel = std::make_unique<ViewportPanel>(
        Workspace->Layout, Workspace->Interaction.Marquee, Workspace->Interaction.Overlay,
        RenderFeature->GetViewportTargets(), "Ortho", DockSlot::CenterBottom, 1.0f, orthoId);
    OrthoPanel = orthoPanel.get();
    UiFeature->AddPanel(std::move(orthoPanel));
    auto editorConsole = std::make_unique<EditorConsolePanel>(debug.GetLogSink(), console);
    ConsolePanel = editorConsole.get();
    ConsolePanel->SetVisible(consoleOpenOnStart);
    UiFeature->AddPanel(std::move(editorConsole));
    UiFeature->AddPanel(std::make_unique<WorldPartitionPanel>(
        Workspace->World, Workspace->Selection, *Commands,
        Workspace->CreationRecipes));
    UiFeature->AddPanel(std::make_unique<GraphViewerPanel>(
        Workspace->World, Workspace->Selection, *Commands, Workspace->Layout));
    UiFeature->AddPanel(std::make_unique<SceneHierarchyPanel>(
        Workspace->World, Workspace->Selection, *Commands));
    UiFeature->AddPanel(std::make_unique<InspectorPanel>(
        Workspace->World, Workspace->Selection, *Commands,
        Workspace->Affordances->Registry()));
    auto cookProfiles = std::make_unique<CookProfilesPanel>(
        Project ? &*Project : nullptr);
    cookProfiles->SetVisible(false);
    if (CookRuntime)
        CookRuntime->SetProfilesPanel(cookProfiles.get());
    UiFeature->AddPanel(std::move(cookProfiles));
    const auto previewBuilder = [this]() -> SceneRenderQueueBuilder* {
        return RenderFeature != nullptr ? RenderFeature->FocusQueueBuilder() : nullptr;
    };
    UiFeature->AddPanel(std::make_unique<LightingPanel>(
        RenderFeature->ShadowReadout(), Workspace->Selection, *Commands,
        [this] { if (RenderFeature != nullptr) RenderFeature->InvalidateShadows(); },
        [this]() -> std::uint32_t {
            return LightingReadModel::CountDirectBakeLights(
                Workspace->World.FocusDocument().GetRegistry().Components);
        },
        [this, previewBuilder]() -> LightingPanel::BakedPreviewState {
            SceneRenderQueueBuilder* builder = previewBuilder();
            const CookSession::Record* record =
                CookRuntime != nullptr ? CookRuntime->LastRecord() : nullptr;
            if (builder == nullptr || record == nullptr)
                return LightingPanel::BakedPreviewState::Unavailable;
            if (!builder->LightmapPreviewEnabled() || !builder->LightmapPreviewLoaded())
                return LightingPanel::BakedPreviewState::Off;
            return builder->LightmapPreviewStale()
                ? LightingPanel::BakedPreviewState::Stale
                : LightingPanel::BakedPreviewState::Fresh;
        },
        [this, previewBuilder](bool enabled) {
            SceneRenderQueueBuilder* builder = previewBuilder();
            if (builder == nullptr)
                return;
            if (enabled && CookRuntime != nullptr)
                CookRuntime->RefreshPreviewNow(*builder);
            builder->SetLightmapPreviewEnabled(enabled);
        },
        [this]() -> LightingPanel::ProbeSummary {
            LightingPanel::ProbeSummary summary;
            summary.AuthoredVolumes = LightingReadModel::CountAuthoredIrradianceVolumes(
                Workspace->World.FocusDocument().GetRegistry().Components);
            if (const CookSession::Record* record =
                    CookRuntime != nullptr ? CookRuntime->LastRecord() : nullptr)
            {
                summary.HasCook = true;
                summary.CookedVolumes = record->ProbeVolumeCount;
                summary.CookedProbes = record->ProbeCount;
            }
            return summary;
        }));
    UiFeature->AddPanel(std::make_unique<ToolPropertiesPanel>(
        [this]() -> IMeshEditTarget* { return Workspace->Interaction.Sink.get(); },
        [this]() -> ManipulationSink* { return Workspace->Interaction.Sink.get(); },
        [this]() -> ToolRegistry* { return Workspace->Interaction.Tools.get(); },
        Workspace->Selection, Workspace->MeshEdit, *Commands,
        Workspace->World, Workspace->ActiveMaterial, Workspace->UvClipboard,
        Workspace->Actions, Workspace->BridgeEdit, Workspace->ElementEdit,
        // Export is the one verb that needs a native file dialog, so the shell
        // keeps it; the geometry itself comes from the selection actions.
        [this] { ExportSelectionGlb(); }));

    // Material picking surfaces: thumbnail residency for both panels, bounded
    // by the budget cvar so a large library never pins every base color
    // texture on the GPU.
    console.Registry().RegisterCVar({
        .Name = "editor.materials.thumbnail_budget",
        .Owner = "editor",
        .Type = CVarType::Int,
        .DefaultValue = std::int64_t{ 128 },
        .CurrentValue = std::int64_t{ 128 },
        .Flags = CVarFlags::Archive,
        .Help = "Max GPU-resident material thumbnails in the browser cache.",
        .Source = { "editor" },
    });
    console.Registry().RegisterCVar({
        .Name = "editor.materials.thumbnail_size",
        .Owner = "editor",
        .Type = CVarType::Double,
        .DefaultValue = 96.0,
        .CurrentValue = 96.0,
        .Flags = CVarFlags::Archive,
        .Help = "Material browser cell size in pixels.",
        .Source = { "editor" },
    });
    Thumbnails = std::make_unique<MaterialThumbnailCache>(
        Assets->Assets, Assets->Textures, engine.Graphics().Images, engine.Graphics().Samplers,
        Assets->Registry);

    // Added after ToolPropertiesPanel so the left column's Down-pack puts it
    // below the tool properties, and before the browser so its previews are
    // always fresher than the browser's trim.
    UiFeature->AddPanel(std::make_unique<ActiveMaterialPanel>(
        Workspace->ActiveMaterial, *Thumbnails,
        [this] { if (Browser != nullptr) Browser->Reveal(); }));

    auto browserPanel = std::make_unique<MaterialBrowserPanel>(
        *Materials, *Thumbnails, Workspace->ActiveMaterial, console.Registry(),
        [this] { Workspace->ApplyActiveMaterialToSelectedFaces(); });
    Browser = browserPanel.get();
    UiFeature->AddPanel(std::move(browserPanel));

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
    explicit SourceWatchState(JobSystem* jobs)
        : TextureImporter(jobs)
    {
    }

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
    SourceWatch = std::make_unique<SourceWatchState>(&engine.Jobs());
    SourceWatch->Importers.Register(SourceWatch->TextureImporter);
    for (const std::string& root : Project->ContentRoots)
    {
        auto watch = std::unique_ptr<SourceWatchState::RootWatch>(new SourceWatchState::RootWatch{
            AssetSourceWatcher(engine.Logging(), root, { ".smat", ".png", ".meta" }),
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

    if (CookRuntime != nullptr)
        CookRuntime->Update(RenderFeature != nullptr ? RenderFeature->FocusQueueBuilder() : nullptr);

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
                {
                    // An import-settings sidecar edit recooks its source.
                    std::string_view source = changed;
                    if (source.ends_with(kImportSettingsSuffix))
                        source.remove_suffix(kImportSettingsSuffix.size());
                    root->Reloader.ReloadSource(source);
                }
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

    // A hidden viewport panel is never drawn, so it cannot clear its own stale
    // on-screen rect; do it here so ResolveAt never routes input to an
    // invisible view (and the render feature skips its offscreen target).
    for (ViewportPanel* panel : { PerspectivePanel, OrthoPanel })
        if (panel != nullptr && !panel->IsVisible())
            panel->ClearViewportRegion();

    // One LRU tick per frame, before the UI panels request thumbnails.
    if (Thumbnails)
        Thumbnails->BeginFrame();
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




void EditorServices::ExportSelectionGlb()
{
    if (Window == nullptr || Window->GetHandle() == nullptr)
        return;

    // The geometry comes from the selection actions (baked in local space
    // through the same kernel as the level cook); the dialog is this layer's.
    const BrushMesh* mesh = Workspace->Actions.SelectedExportMesh();
    if (mesh == nullptr)
    {
        std::fprintf(stderr, "[editor] export: select a brush or baked brush first\n");
        return;
    }

    auto payload = std::make_unique<GlbExportPayload>();
    std::string error;
    if (!BakeBrushToGeometry(*mesh, Workspace->ActiveDocument().GetDefaultMaterial(),
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

    MountProjectContent(*Project, *Assets, logging, &engine.Jobs());
}

void EditorServices::UnloadGameModule()
{
    if (!GameModule.IsValid())
        return;

    // Retract the serializers while the module is still mapped, then unmap.
    GameModule.Instance->OnUnregisterComponents(DefaultComponentSerializerRegistry());
    ModuleLoader.Unload(GameModule);
}
