#include "LevelWorkspace.h"

#include "viewport/EditorViewportCameraSystem.h"
#include "editmodes/ManipulatorSession.h"
#include "workspace/BrushManipulationSink.h"
#include "input/KeymapFile.h"
#include "tools/ToolRegistry.h"
#include "input/ViewportToolDispatcher.h"
#include "input/OriginViewportStamp.h"
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

#include <world/ComponentRegistrar.h>
#include "render/EditorRenderFeature.h"
#include "ui/ActiveMaterialPanel.h"
#include "ui/CookProfilesModal.h"
#include "ui/InspectorSurface.h"
#include <ui/UiService.h>
#include "ui/CookProfilesPanel.h"
#include "ui/EditorStatusBar.h"
#include "ui/EditorToolbar.h"
#include "ui/EditorUiFeature.h"
#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeControls.h"
#include "document/commands/SceneInstanceCommands.h"
#include "ui/InspectorPanel.h"
#include "ui/LightingPanel.h"
#include "ui/MaterialPickerPanel.h"
#include "render/RenderFeatureDetach.h"
#include "documents/DocumentSourceSet.h"
#include "ui/DocumentSaveReportView.h"
#include "ui/MaterialThumbnailCache.h"
#include "ui/ToolPropertiesPanel.h"
#include "render/SceneThumbnailCache.h"
#include "ui/SceneBrowserPanel.h"
#include "ui/SceneHierarchyPanel.h"
#include "ui/WorldPartitionPanel.h"
#include "ui/GraphViewerPanel.h"
#include "ui/ViewportPanel.h"

#include <SDL3/SDL.h>

#include "project/ProjectContentMount.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <assets/cook/TextureCook.h>
#include <render/LightComponentTypes.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <variant>
#include <vector>


namespace
{
// The viewport feature's teardown frees ImGui descriptor sets through the
// backend the window's UI feature owns, so it depends on it and tears down
// first.
constexpr std::string_view kEditorRenderFeatureId = "editor_render";
constexpr std::string_view kEditorUiFeatureId = "editor_ui";
constexpr std::array<std::string_view, 1> kEditorRenderDependsOn{ kEditorUiFeatureId };
} // namespace

LevelWorkspace::LevelWorkspace(Engine& engine,
                               SdlWindow& window,
                               ProjectDescriptor* project,
                               Game* module,
                               MaterialLibrary& materials,
                               DocumentSourceSet& documents)
    : Documents(documents)
    , Materials(&materials)
    , Project(project)
{
    EnginePtr = &engine;
    Window = &window;
    PrimaryWindow = &window;
    Assets = &engine.Content().Assets();

    RegisterDocumentSerializers();
    // Before the document is created, so its World registers storage for the
    // module's components.
    RegisterModuleComponents(module);

    BuildDocument();
    // After the document: an authored workflow presents editor state, and the
    // inspector's is the selection and the command stack the document owns.
    BuildAuthoredWorkflows();
    BuildPlayLoop();
    BuildFileActions();
    BuildInput();
    BuildViewportRendering();
    BuildUi();
}

LevelWorkspace::~LevelWorkspace()
{
    if (Window != nullptr)
        SetRelativeMouseMode(*Window, false);

    // The panels and chrome first: they hold references into everything below.
    Surface = WorkspaceView{};

    // The cook runtime and Files reference Workspace/Commands/Materials/Project;
    // tear them down before that state goes away.
    Files.reset();
    CookRuntime.reset();
    // Before Workspace, not after: the render feature holds references straight
    // into it -- the world document, the viewport layout, grid and world-view
    // settings, a lambda reading the manipulator session, and five sub-renderers
    // bound to selection, mesh-edit, overlay, preview and affordance state. Its
    // Teardown touches none of them today, which is the only reason the previous
    // order survived; nothing enforced that, and the next line added to Teardown
    // would have made it a use-after-free. The renderer would otherwise hold the
    // feature until ~Renderer, long after all of this is gone.
    DetachRenderFeature(*EnginePtr, RenderFeature);
    LevelDocument.reset();
    Workspace.reset();
    // After the documents, not before: their worlds hold things the module
    // compiled, and the document destructor is what runs them. The module
    // itself is unmapped by its owner, after this.
    RetractModuleComponents();
    Commands.reset();
    Router.reset();
    Navigation.reset();
    Shortcuts.reset();
    // The thumbnail bindings release texture refs through the asset stack and
    // free ImGui descriptor sets (the panels referencing the cache never touch
    // it in their destructors).
    Thumbnails.reset();
    // Toolbar and StatusBar release with the object in reverse declaration
    // order; neither touches the subsystems reset above.
}

void LevelWorkspace::BuildDocument()
{
    Engine& engine = *EnginePtr;
    Commands = std::make_unique<CommandStack>();
    Workspace = std::make_unique<EditorWorkspace>(engine.Logging(), *Commands);
    CameraSystem = std::make_unique<EditorViewportCameraSystem>(Workspace->Layout);
    LevelDocument = std::make_unique<LevelDocumentSource>(*Workspace, *Commands, Documents);
    if (Assets)
        Workspace->World.SetAssetEnvironment(*Assets);
    Workspace->Layout.OnResize(Window->GetExtent().Width, Window->GetExtent().Height);
}

void LevelWorkspace::BuildPlayLoop()
{
    Engine& engine = *EnginePtr;
    CookRuntime = std::make_unique<EditorCookRuntime>(engine, Workspace->World,
                                                      Project ? &*Project : nullptr,
                                                      Assets ? &*Assets : nullptr);
    CookRuntime->RegisterConsoleCommands(engine.Console().Registry());
    CookRuntime->SetSaveGate([this](std::string& error) { return SaveBeforeLaunch(error); });
}

void LevelWorkspace::BuildFileActions()
{
    Engine& engine = *EnginePtr;
    std::vector<std::string> contentRoots;
    if (Project)
        contentRoots = Project->ContentRoots;
    // Scene instances resolve their asset:// sources against the same roots.
    {
        std::vector<std::filesystem::path> sourceRoots;
        for (const std::string& root : contentRoots)
            sourceRoots.emplace_back(root);
        Workspace->World.SetContentRoots(std::move(sourceRoots));
    }
    // Baking writes a mesh asset into the project, so the selection actions get
    // the asset environment once it exists (they stay inert without one).
    if (Assets && Project && !Project->ContentRoots.empty())
        Workspace->Actions.SetAssetEnvironment(*Assets, Project->ContentRoots.front(),
                                               engine.Logging());
    Files = std::make_unique<DocumentFileActions>(
        *Window, Workspace->World, [this] { Workspace->ResolvePendingEdits(); },
        *Materials, std::move(contentRoots), Workspace->Selection, Workspace->MeshEdit);
    Files->RegisterCommands(engine.Console().Registry());
}

void LevelWorkspace::BuildInput()
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
        { "edit.undo",             SDLK_Z,      { .Ctrl = true },                [this] { if (!UndoStagedEdit()) Documents.Undo(); } },
        { "edit.redo",             SDLK_Z,      { .Ctrl = true, .Shift = true }, [this] { Documents.Redo(); } },
        { "edit.redo",             SDLK_Y,      { .Ctrl = true },                [this] { Documents.Redo(); } },
        { "edit.delete",           SDLK_DELETE, {},                              [this] { Workspace->DeleteSelection(); } },
        { "edit.dissolve",         SDLK_BACKSPACE, {},                           [this] { Workspace->DissolveSelectedEdges(); } },
        { "edit.select_all",       SDLK_A,      { .Ctrl = true },                [this] { Workspace->SelectAll(); } },
        { "edit.duplicate",        SDLK_D,      { .Ctrl = true },                [this] { Workspace->Actions.Duplicate(/*asInstance*/ false); } },
        { "edit.duplicate_instance", SDLK_D,    { .Alt = true },                 [this] { Workspace->Actions.Duplicate(/*asInstance*/ true); } },
        { "edit.repeat",           SDLK_R,      { .Ctrl = true },                [this] { Workspace->Actions.RepeatLast(); } },
        { "edit.escape",           SDLK_ESCAPE, {},                              [this] { Workspace->EscapeStep(); } },
        { "file.new",              SDLK_N,      { .Ctrl = true },                [this] { AfterSettlingChanges([this] { Files->New(); }); } },
        { "file.open",             SDLK_O,      { .Ctrl = true },                [this] { AfterSettlingChanges([this] { Files->RequestOpen(); }); } },
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

    // A wheel's key is held, not pressed, so it is not a shortcut row: the
    // session owns both edges of it. The action names are what a keymap file
    // rebinds, like any other. Both wheels place themselves against the same
    // window and open over the same scene test.
    {
        const auto chordFor = [&](std::string_view action, SDL_Keycode fallback)
        {
            if (const auto it = overrides.find(std::string(action)); it != overrides.end())
                return it->second;
            return KeyChord{ .Key = fallback, .Mods = {} };
        };
        const auto frame = []
        {
            // The one place a wheel learns the window and the UI scale:
            // captured together at open, resolved once into its layout.
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            return RadialMenu::Frame{
                .Scale = EditorUi::UiScale,
                .Min = vp->WorkPos,
                .Max = ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
            };
        };
        const auto pointerOnScene = [this](ImVec2 pointer)
        {
            // The viewport whose rect holds the pointer, and that same
            // viewport's panel saying nothing is drawn over it: one
            // viewport, one hover flag. Which view is active or was
            // focused last plays no part.
            const ViewportId under = Workspace->Layout.ResolveAt(pointer);
            if (!under.IsValid())
                return false;
            for (const ViewportPanel* panel : { PerspectivePanel, OrthoPanel })
            {
                if (panel != nullptr && panel->GetViewportId() == under)
                    return panel->IsViewportRegionHovered();
            }
            return false;
        };
        ToolMenu = std::make_unique<ToolRegistryMenuModel>(*Workspace->Interaction.Tools);
        ToolWheel = std::make_unique<RadialMenuSession>(*ToolMenu, chordFor("tool.wheel", SDLK_Q), frame, pointerOnScene);
        GizmoMenu = std::make_unique<TransformModeMenuModel>([this] { return Workspace->Interaction.Manipulators; });
        GizmoWheel = std::make_unique<RadialMenuSession>(*GizmoMenu, chordFor("gizmo.wheel", SDLK_Z), frame, pointerOnScene);
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
            const UiInputCapture shell =
                UiFeature != nullptr ? UiFeature->GetInputCapture() : UiInputCapture{};
            const bool overViewport =
                (PerspectivePanel != nullptr && PerspectivePanel->IsViewportRegionHovered())
                || (OrthoPanel != nullptr && OrthoPanel->IsViewportRegionHovered());
            UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr;
            return CombineUiCapture(shell, overViewport,
                                    ui != nullptr ? ui->Capture() : UiInputCapture{});
        }));
    // The wheels sit under the guard (a focused text field keeps its letters)
    // and above everything else: an open wheel owns the pointer and the keys,
    // which is also what keeps the other wheel closed, and each yields its key
    // to any gesture already holding the pointer.
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return ToolWheel->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return GizmoWheel->OnInput(e, cap); });
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
                const bool uiOwnsInput = kind != PointerCaptureKind::Exclusive;
                UiFeature->SetMouseInputEnabled(uiOwnsInput);
                UiFeature->SetKeyboardInputEnabled(uiOwnsInput);
            }
        });
}

void LevelWorkspace::BuildViewportRendering()
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

    // The interactive limit on pieces one brush's modifier stack may evaluate to
    // (read per frame by EditorRenderFeature). Preview only: the cook uses the
    // compile-time hard limit, so this never changes whether a level cooks.
    console.Registry().RegisterCVar({
        .Name = "editor.brush.preview_piece_budget",
        .Owner = "editor",
        .Type = CVarType::Int,
        .DefaultValue = 4096,
        .CurrentValue = 4096,
        .Flags = CVarFlags::Archive,
        .Help = "Editor: max pieces a brush's modifier stack evaluates to in the viewport preview.",
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
    // Staged, not added: the commit happens at the end of BuildUi, once the UI
    // feature this one depends on has been staged too. The pointer is good only
    // if the commit reports the id succeeded.
    Renderer& renderer = engine.Graphics().MainRenderer;
    RenderFeature = renderer.StageFeature(
        std::move(renderFeature),
        FeatureRegistration{ .Id = kEditorRenderFeatureId,
                             .DependsOn = kEditorRenderDependsOn });
    // Committed before any panel is built against it, so a feature that fails
    // to set up leaves nothing holding its caches.
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
    {
        std::fprintf(stderr, "[editor] viewport render feature failed to set up; "
                             "viewports will not draw\n");
        RenderFeature = nullptr;
    }
}

void LevelWorkspace::BuildUi()
{
    Engine& engine = *EnginePtr;
    ConsoleService& console = engine.Console();

    // Default layout proportions: mesh tools over the active material in a
    // narrow left column, the perspective viewport dominating the center with
    // the ortho view + Materials/Console strip under it, world/hierarchy row
    // over the inspector on the right.
    Surface.Layout = DockLayoutRatios{
        .LeftEdge = 0.06f,
        .Left = 0.16f,
        .Right = 0.3f,
        .CenterBottom = 0.35f,
        .RightBottom = 0.3f,
    };
    Surface.File = WorkspaceFileActions{
        .New = [this] { AfterSettlingChanges([this] { Files->New(); }); },
        .NewWorld = [this] { AfterSettlingChanges([this] { Files->NewWorld(); }); },
        .Open = [this] { AfterSettlingChanges([this] { Files->RequestOpen(); }); },
        .Save = [this] { Files->Save(); },
        .SaveAs = [this] { Files->RequestSaveAs(); },
    };
    Surface.Status = [this] { return Files->DocumentLabel(); };

    // The editing toolbar's presentation, hosted by the perspective viewport;
    // the workspace bar and the status bar are the fixed bars of app chrome,
    // registered before the panels so the space they reserve is subtracted
    // from the work area.
    Toolbar = std::make_unique<EditorToolbar>(
        [this] { return Workspace->Interaction.Tools.get(); },
        [this] { return Workspace->Interaction.Manipulators; },
        Workspace->MeshEdit, Workspace->Grid, Workspace->WorldView);
    // The Cook/Play/Stop group routes through the same paths as the cook/play/stop
    // console commands.
    // A cook reads the live documents (and force-saves the world first), so open
    // previews settle before it starts or they would cook half-staged.
    CookPlay = std::make_unique<CookPlayControls>(CookPlayControls::Actions{
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
            std::vector<CookPlayControls::ProfileChoice> choices;
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
        // The authored workflow, beside the ImGui panel rather than instead of
        // it. Both stay until one is demonstrably better.
        .OpenAuthoredProfiles = [this] {
            if (ProfilesModal)
                ProfilesModal->Open();
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
    Surface.BarControls = WorkspaceBarControls{
        .Width = [this] { return CookPlay->Width(EditorChrome::BarButtonSize()); },
        .Draw = [this] { CookPlay->Draw(EditorChrome::BarButtonSize()); },
        .Lit = [this] { return CookPlay->IsBusy(); },
    };
    Surface.AddPanel(std::make_unique<ToolPalettePanel>([this] { return Workspace->Interaction.Tools.get(); }));
    Toolbar->SetSurfaceProvider([this] {
        return UiFeature != nullptr ? UiFeature->SurfaceFor(BarRole::Toolbar) : EditorChrome::BarSurface{};
    });
    // The workspace bar sits under the caption as app chrome, on the primary
    // viewport's header plate, so it reads apart from the editing row.
    Surface.Chrome.push_back([this] { StatusBar->Draw(); });
    Surface.Overlays.push_back([this] { ChangesPrompt.Draw(); });
    Surface.Overlays.push_back([this] {
        constexpr const char* title = "Documents not saved";
        if (ShowSaveReport && !ImGui::IsPopupOpen(title))
            ImGui::OpenPopup(title);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextUnformatted("Every document is saved before a cook or a play session reads the disk.");
        DrawDocumentSaveReport(Documents, SettleError);
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ShowSaveReport = false;
            SettleError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    });
    Surface.Overlays.push_back([this]
    {
        DrawRadialMenu(*ToolWheel, *ToolMenu);
        DrawRadialMenu(*GizmoWheel, *GizmoMenu);
    });

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
    // The viewport and lighting panels read state the render feature owns, and
    // staging hands back nothing when the renderer never came up. Skipping them
    // leaves an editor with no viewports, which is what a dead renderer means
    // anyway; dereferencing would just make it a crash instead of a message.
    if (RenderFeature != nullptr)
    {
        // A scene dropped from the browser lands where the cursor points:
        // the nearest brush surface, or the working grid where the ray misses
        // everything. One undoable command, the placement's root selected.
        const auto placeDroppedScene =
            [this](ViewportId viewportId, ImVec2 position, std::string_view source)
        {
            const EditorViewport* viewport = Workspace->Layout.Find(viewportId);
            if (viewport == nullptr || Commands == nullptr)
                return;
            EditorDocument& document = Workspace->World.FocusDocument();
            Vec3d point{};
            if (const std::optional<SurfaceHit> hit = Workspace->Picking.PickSurface(
                    *viewport, position, document.GetScene()))
            {
                point = hit->Point;
            }
            else if (const std::optional<Vec3d> onGrid =
                         Workspace->Picking.ProjectPointToGrid(*viewport, position,
                                                               Workspace->Grid))
            {
                point = *onGrid;
            }
            else
            {
                return;
            }
            Transform3f placement = Transform3f::Identity();
            placement.Position = point;
            Commands->Execute(std::make_unique<PlaceSceneInstanceCommand>(
                std::string(source), placement, document, Workspace->Selection));
        };

        auto perspectivePanel = std::make_unique<ViewportPanel>(
            Workspace->Layout, Workspace->Interaction.Marquee, Workspace->Interaction.Overlay,
            RenderFeature->GetViewportTargets(), "VIEWPORT", DockSlot::Center, 1.0f,
            PanelStyle::ViewportPrimary,
            // The central node has no tab bar and no View entry: nothing can hide it.
            PanelPersistence{ "viewport", PanelVisibilityPolicy::SessionOnly }, perspectiveId);
        perspectivePanel->SetSceneDropHandler(placeDroppedScene);
        // This viewport's header is the editing toolbar: one row in place of
        // a title, above the scene, with the gizmo strip on the window's
        // midline, where the eye rests, rather than the pane's own.
        perspectivePanel->SetHeaderRows({
            {
                .Height = [] { return EditorToolbar::ViewportRowHeight(); },
                .Draw = [this](ImDrawList* dl, ImVec2 mn, ImVec2 mx)
                {
                    const ImGuiViewport* window = ImGui::GetMainViewport();
                    Toolbar->DrawViewportRow(dl, mn, mx, window->Pos.x + window->Size.x * 0.5f);
                },
            },
        });
        PerspectivePanel = perspectivePanel.get();
        Surface.AddPanel(std::move(perspectivePanel));
        auto orthoPanel = std::make_unique<ViewportPanel>(
            Workspace->Layout, Workspace->Interaction.Marquee, Workspace->Interaction.Overlay,
            RenderFeature->GetViewportTargets(), "ORTHO", DockSlot::CenterBottom, 1.0f,
            PanelStyle::Viewport, PanelPersistence{ "ortho", PanelVisibilityPolicy::Remembered }, orthoId);
        orthoPanel->SetSceneDropHandler(placeDroppedScene);
        OrthoPanel = orthoPanel.get();
        Surface.AddPanel(std::move(orthoPanel));
    }
    else
    {
        std::fprintf(stderr, "[editor] no viewport render feature; "
                             "viewport panels are unavailable\n");
    }
    Surface.AddPanel(std::make_unique<WorldPartitionPanel>(
        Workspace->World, Workspace->Selection, *Commands,
        Workspace->CreationRecipes));
    Surface.AddPanel(std::make_unique<GraphViewerPanel>(
        Workspace->World, Workspace->Selection, *Commands, Workspace->Layout));
    Surface.AddPanel(std::make_unique<SceneHierarchyPanel>(
        Workspace->World, Workspace->Selection, *Commands,
        [this](const std::string& assetPath)
        { return Files != nullptr && Files->OpenSceneSource(assetPath); }));
    {
        std::vector<std::filesystem::path> sceneRoots;
        if (Project)
            for (const std::string& root : Project->ContentRoots)
                sceneRoots.emplace_back(root);
        // The thumbnail cache exists only after the render feature's Setup;
        // the accessor resolves lazily and hands the roots over exactly once.
        std::vector<std::filesystem::path> thumbnailRoots = sceneRoots;
        auto thumbnails = [this, roots = std::move(thumbnailRoots),
                           handed = false]() mutable -> SceneThumbnailCache*
        {
            SceneThumbnailCache* cache =
                RenderFeature != nullptr ? RenderFeature->SceneThumbnails() : nullptr;
            if (cache != nullptr && !handed)
            {
                cache->SetContentRoots(roots);
                handed = true;
            }
            return cache;
        };
        Surface.AddPanel(std::make_unique<SceneBrowserPanel>(
            Workspace->World, Workspace->Selection, *Commands,
            std::move(sceneRoots), std::move(thumbnails)));
    }
    Surface.AddPanel(std::make_unique<InspectorPanel>(
        Workspace->World, Workspace->Selection, *Commands,
        Workspace->Affordances->Registry()));
    auto cookProfiles = std::make_unique<CookProfilesPanel>(
        Project ? &*Project : nullptr);
    cookProfiles->SetVisible(false);
    if (CookRuntime)
        CookRuntime->SetProfilesPanel(cookProfiles.get());
    Surface.AddPanel(std::move(cookProfiles));
    const auto previewBuilder = [this]() -> SceneRenderQueueBuilder* {
        return RenderFeature != nullptr ? RenderFeature->FocusQueueBuilder() : nullptr;
    };
    // Same reason as the viewport panels: the shadow readout is the render
    // feature's own state, so there is nothing to show without it.
    if (RenderFeature != nullptr)
    {
        Surface.AddPanel(std::make_unique<LightingPanel>(
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
    }
    Surface.AddPanel(std::make_unique<ToolPropertiesPanel>(
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
        Assets->Assets, *Assets->Textures, engine.Graphics().Images, engine.Graphics().Samplers,
        Assets->Registry);

    // Added after ToolPropertiesPanel so the left column's Down-pack puts it
    // below the tool properties, and before the browser so its previews are
    // always fresher than the browser's trim.
    Surface.AddPanel(std::make_unique<ActiveMaterialPanel>(
        Workspace->ActiveMaterial, *Thumbnails,
        [this] { if (Browser != nullptr) Browser->Reveal(); }));

    auto browserPanel = std::make_unique<MaterialPickerPanel>(
        *Materials, *Thumbnails, Workspace->ActiveMaterial, console.Registry(),
        [this] { Workspace->ApplyActiveMaterialToSelectedFaces(); });
    Browser = browserPanel.get();
    Surface.AddPanel(std::move(browserPanel));
}

bool LevelWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return document.Source == LevelDocument.get();
}

bool LevelWorkspace::UndoStagedEdit()
{
    // A live preview is undone by itself, as the command stack defines: the
    // committed step before it stays for the next undo.
    if (!Commands->HasPendingEdit())
        return false;
    Commands->Undo();
    return true;
}

bool LevelWorkspace::HasStagedEdit() const
{
    return Commands->HasPendingEdit();
}

void LevelWorkspace::AfterSettlingChanges(std::function<void()> proceed)
{
    std::vector<DocumentRef> changed;
    LevelDocument->AppendChangedDocuments(changed);
    ChangesPrompt.Ask(!changed.empty(), LevelDocument->DocumentLabel(LevelDocumentSource::kKey),
                      [this, proceed = std::move(proceed)](DirtyDisposition disposition) {
                          if (disposition == DirtyDisposition::Save)
                          {
                              const DocumentSaveResult saved = Documents.Save(LevelDocument->Ref());
                              if (saved.Status != DocumentSaveStatus::Saved
                                  && saved.Status != DocumentSaveStatus::SavedWithProblems)
                              {
                                  ShowSaveReport = true;
                                  return;
                              }
                          }
                          else if (disposition == DirtyDisposition::Discard)
                          {
                              LevelDocument->DiscardDocument(LevelDocumentSource::kKey);
                          }
                          proceed();
                      });
}

bool LevelWorkspace::SaveBeforeLaunch(std::string& error)
{
    const std::vector<DocumentRef> changed = Documents.ChangedDocuments();
    (void)Documents.SaveAll();
    // Only what this save wrote decides: an older failure for a document saved
    // since by other means is not a reason to stop.
    for (const DocumentRef& document : changed)
    {
        const DocumentSaveResult* result = Documents.LastSave().Find(document);
        if (result != nullptr && (result->Status == DocumentSaveStatus::Conflict
                                  || result->Status == DocumentSaveStatus::Failed))
        {
            error = "a document could not be saved";
            ShowSaveReport = true;
            return false;
        }
    }
    return true;
}

void LevelWorkspace::Tick(FrameUpdateContext& ctx)
{
    // The layout follows the window it is placed in, whether or not it is
    // showing, so it is right the moment its tab comes forward.
    if (Window != nullptr)
    {
        const WindowExtent extent = Window->GetExtent();
        if (extent.Width != LayoutExtent.Width || extent.Height != LayoutExtent.Height)
        {
            Workspace->Layout.OnResize(extent.Width, extent.Height);
            LayoutExtent = extent;
        }
    }
    // The fly camera reads held keys, which belong to the workspace in front.
    if (Visible)
        CameraSystem->FrameUpdate(ctx);
    ProcessFrame();
}

void LevelWorkspace::Place(EditorUiFeature& window)
{
    UiFeature = &window;
    if (Window != nullptr && Window != &window.GetWindow())
        SetRelativeMouseMode(*Window, false);
    Window = &window.GetWindow();
    if (Files != nullptr)
        Files->SetWindow(*Window);
}

void LevelWorkspace::SetVisible(bool visible)
{
    if (Visible == visible)
        return;
    Visible = visible;
    // Going to the background is a focus loss: a fly-look releases the
    // pointer and a drag in flight is cancelled rather than left holding it.
    if (!visible && Router != nullptr)
    {
        InputEvent focusLost = FocusLostEvent{};
        (void)Router->Route(focusLost);
    }
}

void LevelWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
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

void LevelWorkspace::BuildAuthoredWorkflows()
{
    UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr;
    if (ui == nullptr || !ui->IsReady() || Project == nullptr)
        return;

    // The window, as authored UI sees it. One surface for every authored screen
    // Kyusu opens, because focus and modality are arbitrated within a surface:
    // a dialog on a surface of its own would take focus from nothing. Tracked
    // against the window in ProcessFrame, since a retained document re-flows on
    // a resize where a baked atlas cannot.
    if (!AuthoredSurface.IsValid() && PrimaryWindow != nullptr)
    {
        AuthoredSurface = ui->CreateSurface(
            "kyusu",
            RenderExtent{ PrimaryWindow->GetExtent().Width, PrimaryWindow->GetExtent().Height });
    }
    if (!AuthoredSurface.IsValid())
        return;

    ProfilesModal = std::make_unique<CookProfilesModal>(*ui, AuthoredSurface, &*Project);
    if (Workspace != nullptr && Commands != nullptr)
    {
        Inspector = std::make_unique<InspectorSurface>(
            *ui, AuthoredSurface, Workspace->World, Workspace->Selection, *Commands,
            Workspace->Affordances->Registry());
    }

    // Openable from the console as well as the menu, so a startup script can
    // bring it up -- which is how it gets captured and looked at without a
    // person driving a menu.
    EnginePtr->Console().Registry().RegisterCommand({
        .Name = "editor.inspector.authored",
        .Owner = "editor",
        .Usage = "editor.inspector.authored [close]",
        .Help = "Open the authored inspector, the RML document that presents the "
                "selected entity's components beside the ImGui panel.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (Inspector == nullptr)
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error("the authored inspector is unavailable");
                return result;
            }
            if (!args.empty() && args[0] == "close")
            {
                Inspector->Close();
                result.Info("closed the authored inspector");
            }
            else
            {
                Inspector->Open();
                result.Info("opened the authored inspector");
            }
            return result;
        },
    });

    EnginePtr->Console().Registry().RegisterCommand({
        .Name = "editor.profiles.authored",
        .Owner = "editor",
        .Usage = "editor.profiles.authored [close]",
        .Help = "Open the authored cook-profile workflow, the RML document that "
                "coexists with the ImGui panel while it earns its place.",
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (ProfilesModal == nullptr)
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error("authored cook profiles are unavailable");
                return result;
            }
            if (!args.empty() && args[0] == "close")
            {
                ProfilesModal->Close();
                result.Info("closed the authored cook profiles");
            }
            else
            {
                ProfilesModal->Open();
                result.Info("opened the authored cook profiles");
            }
            return result;
        },
    });
}

void LevelWorkspace::DrawRadialMenu(const RadialMenuSession& wheel, const IRadialMenuModel& menu)
{
    if (wheel.GetPhase() != RadialMenuPhase::Open)
        return;

    // Every position comes from the session's layout, the same numbers it
    // hit-tests; this only pairs each slot with what the model says it looks
    // like. Labels are the model's storage, null-terminated where they come
    // from tables and registries.
    const RadialMenu::Layout& layout = wheel.GetLayout();
    const int hot = wheel.GetHot();
    const int hotVariant = wheel.GetHotVariant();
    const int active = menu.ActiveIndex();
    const int count = menu.Count();
    std::vector<EditorChrome::WheelSlot> slots;
    slots.reserve(static_cast<std::size_t>(std::max(count, 0)));
    std::string caption;
    for (int index = 0; index < count; ++index)
    {
        const IRadialMenuModel::MenuItem item = menu.Item(index);
        const RadialMenu::Span span = RadialMenu::SectorSpan(index, layout.Count);
        slots.push_back({
            .Center = RadialMenu::SlotCenter(layout, index),
            .Size = layout.Button,
            .Angle0 = span.Begin,
            .Angle1 = span.End,
            .Icon = item.Icon,
            .Label = item.Label.data(),
            .Active = index == active,
            .Hot = index == hot,
        });
        if (index == hot || (hot < 0 && index == active))
            caption = item.Label;
    }

    // The hot entry's variants on the outer ring, from the same layout the
    // session resolves them against.
    std::vector<EditorChrome::WheelSlot> variants;
    if (hot >= 0 && hot < count)
    {
        const std::span<const IRadialMenuModel::MenuItem> choices = menu.Variants(hot);
        const int choiceCount = static_cast<int>(choices.size());
        const int current = menu.ActiveVariant(hot);
        variants.reserve(choices.size());
        for (int v = 0; v < choiceCount; ++v)
        {
            const RadialMenu::Span span = RadialMenu::VariantSpan(layout, hot, v, choiceCount);
            variants.push_back({
                .Center = RadialMenu::VariantSlotCenter(layout, hot, v, choiceCount),
                .Size = layout.Button,
                .Angle0 = span.Begin,
                .Angle1 = span.End,
                .Icon = choices[static_cast<std::size_t>(v)].Icon,
                .Label = choices[static_cast<std::size_t>(v)].Label.data(),
                .Active = v == current,
                .Hot = v == hotVariant,
            });
        }
        if (hotVariant >= 0 && hotVariant < choiceCount)
            caption += std::string(" \xC2\xB7 ") + std::string(choices[static_cast<std::size_t>(hotVariant)].Label);
    }
    // The foreground list draws after every window, floating panels and open
    // menus included, which is where a modal surface belongs.
    EditorChrome::DrawRadialMenu(ImGui::GetForegroundDrawList(), EditorChrome::WheelPaint{
        .Center = layout.Center,
        .Radius = layout.Radius,
        .Hub = layout.Hub,
        .Seam = layout.Seam,
        .Rim = layout.Rim,
        .OuterRadius = layout.OuterRadius,
        .CaptionY = layout.CaptionY(),
        .Slots = slots,
        .Variants = variants,
        .Caption = caption,
        .CaptionDim = hot < 0,
    });
}

void LevelWorkspace::ProcessFrame()
{
    // Before the engine updates the UI: act on what the document asked for and
    // publish what it should now show, so a click and its answer land in the
    // same frame. This hook runs inside FramePhase::Update, which is where the
    // engine guarantees that ordering.
    // A retained document re-flows on a resize, so the surface follows the
    // window rather than latching whatever size it was created at. Unchanged
    // sizes cost a comparison.
    if (AuthoredSurface.IsValid() && PrimaryWindow != nullptr)
    {
        if (UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr; ui != nullptr)
        {
            ui->SetSurfaceSize(AuthoredSurface,
                               RenderExtent{ PrimaryWindow->GetExtent().Width,
                                             PrimaryWindow->GetExtent().Height });
            // The same display scale the shell resolved. Without this an
            // authored surface stays at 1.0 while the ImGui chrome beside it
            // scales, so on a HiDPI display the two halves of the same editor
            // disagree about how big a pixel is. Unchanged values cost a
            // comparison; a change re-flows the documents, which is the whole
            // reason this is live rather than latched.
            ui->SetSurfaceScale(AuthoredSurface, EditorUi::UiScale);
        }
    }

    if (ProfilesModal != nullptr)
        ProfilesModal->Update();
    if (Inspector != nullptr)
        Inspector->Update();

    if (Files)
        Files->ProcessPending();

    if (CookRuntime != nullptr)
        CookRuntime->Update(RenderFeature != nullptr ? RenderFeature->FocusQueueBuilder() : nullptr);

    // Rebuild the transient viewport overlay (selected-brush dimension labels)
    // before the UI panel draws it this frame, and keep the ortho views aligned
    // to the (possibly gizmo-dragged) grid frame.
    if (Workspace && Visible)
    {
        Workspace->UpdateOverlay();
        Workspace->SyncOrthoViewsToGridFrame();
    }

    // A hidden viewport panel, or any panel of a workspace in the background,
    // is never drawn, so it cannot clear its own stale on-screen rect; do it
    // here so ResolveAt never routes input to an invisible view.
    for (ViewportPanel* panel : { PerspectivePanel, OrthoPanel })
        if (panel != nullptr && (!Visible || !panel->IsVisible()))
            panel->ClearViewportRegion();

    // One LRU tick per frame, before the UI panels request thumbnails. The
    // renderer's fence-anchored clock decides when an evicted binding's
    // descriptor set is safe to free; this used to be a local countdown of
    // four, which is one short of the guarantee at the maximum frames-in-flight.
    if (Thumbnails)
    {
        GpuFrameRetirement retirement;
        if (const GraphicsServices* graphics =
                EnginePtr != nullptr ? EnginePtr->TryGraphics() : nullptr)
        {
            retirement = graphics->Frames.GetRetirement();
        }
        Thumbnails->BeginFrame(retirement);
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




void LevelWorkspace::ExportSelectionGlb()
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
            std::unique_ptr<GlbExportPayload> owned(static_cast<GlbExportPayload*>(userdata));
            if (filelist == nullptr || filelist[0] == nullptr)
                return;
            std::filesystem::path path(filelist[0]);
            if (path.extension() != ".glb")
                path += ".glb";
            std::string writeError;
            if (!WriteGlbFile(owned->Geometry, owned->Materials, path, &writeError))
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

void LevelWorkspace::RegisterModuleComponents(Game* module)
{
    if (module == nullptr)
        return;

    // The editor only borrows the module's component serializers (so it can edit
    // scenes containing game components); it never runs the game's lifecycle.
    // No World and no session here, so the registrar carries neither -- a game
    // component that only replicates is registered and simply has nowhere to go.
    ComponentRegistrar registrar(nullptr, &EditorSceneSerializers(), nullptr);
    module->OnRegisterComponents(registrar);
    const std::span<const ComponentTypeId> added = registrar.AddedSerializers();
    GameModuleSerializerTypes.assign(added.begin(), added.end());

    // Storage is what a game component needs to exist in a document; its
    // gameplay vocabulary is what the names inside authored content resolve
    // against. Each document installs it into its own World, so this is held
    // rather than run once.
    SetEditorModuleVocabulary([module](World& world) { module->OnRegisterVocabulary(world); });
}

void LevelWorkspace::RetractModuleComponents()
{
    // The vocabulary installer goes with the serializers: its target is code in
    // the module's image. Documents already built keep the names they were
    // given -- they are values in their own worlds.
    for (ComponentTypeId type : GameModuleSerializerTypes)
        (void)EditorSceneSerializers().Remove(type);
    GameModuleSerializerTypes.clear();
    SetEditorModuleVocabulary({});
}
