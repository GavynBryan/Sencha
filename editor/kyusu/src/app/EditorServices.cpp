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

#include <world/ComponentRegistrar.h>
#include "render/EditorRenderFeature.h"
#include "ui/ActiveMaterialPanel.h"
#include "ui/CookProfilesModal.h"
#include "ui/InspectorSurface.h"
#include <ui/UiService.h>
#include "ui/CookProfilesPanel.h"
#include "ui/EditorConsolePanel.h"
#include "ui/EditorStatusBar.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorToolbar.h"
#include "ui/EditorUiFeature.h"
#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeControls.h"
#include "document/commands/SceneInstanceCommands.h"
#include "ui/InspectorPanel.h"
#include "ui/LightingPanel.h"
#include "ui/MaterialBrowserPanel.h"
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
#include <assets/cook/AssetImporter.h> // kImportSettingsSuffix
#include <assets/cook/ContentImporters.h>
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


#ifndef SENCHA_EDITOR_BRAND_DIR
#define SENCHA_EDITOR_BRAND_DIR "."
#endif

namespace
{
// The editor's two render features and the one edge between them: the render
// feature's teardown frees ImGui descriptor sets through the backend the UI
// feature owns, so it must tear down first. Reverse-resolved teardown gives
// that ordering; before the edge existed it came from registration order.
constexpr std::string_view kEditorRenderFeatureId = "editor_render";
constexpr std::string_view kEditorUiFeatureId = "editor_ui";
constexpr std::array<std::string_view, 1> kEditorRenderDependsOn{ kEditorUiFeatureId };
} // namespace

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
    // After the document: an authored workflow presents editor state, and the
    // inspector's is the selection and the command stack the document owns.
    BuildAuthoredWorkflows();
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
    // Before Workspace, not after: the render feature holds references straight
    // into it -- the world document, the viewport layout, grid and world-view
    // settings, a lambda reading the manipulator session, and five sub-renderers
    // bound to selection, mesh-edit, overlay, preview and affordance state. Its
    // Teardown touches none of them today, which is the only reason the previous
    // order survived; nothing enforced that, and the next line added to Teardown
    // would have made it a use-after-free. The renderer would otherwise hold the
    // feature until ~Renderer, long after all of this is gone.
    if (RenderFeature != nullptr && EnginePtr != nullptr)
    {
        GraphicsServices* graphics = EnginePtr->TryGraphics();
        // Refusal means the feature is still registered and still holding
        // references into what is about to be destroyed. Nothing depends on it
        // today, so this is a guard against a future edge, not a live path.
        if (graphics == nullptr || !graphics->MainRenderer.RemoveFeature(RenderFeature))
        {
            std::fprintf(stderr, "[editor] viewport render feature could not be "
                                 "removed; editor state it borrows is being "
                                 "destroyed underneath it\n");
        }
        RenderFeature = nullptr;
    }
    Workspace.reset();
    // After the documents, not before: their worlds hold things the module
    // compiled -- a locomotion mode's enter and exit closures, a game
    // component's type-erased OnRemove hook and its stable name. Unmapping
    // first leaves every one of those pointing into freed pages, and the
    // document destructor is what runs them.
    UnloadGameModule();
    Commands.reset();
    Router.reset();
    Navigation.reset();
    Shortcuts.reset();
    // Last, and after both: the document's StaticMeshComponents release into
    // these caches as it dies, and the render feature held handles into them
    // too, so Assets has to outlive both. The thumbnail bindings release
    // texture refs through Assets and free ImGui descriptor sets, so they land
    // between (the panels referencing the cache never touch it in their
    // destructors).
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
    // Scene instances resolve their asset:// sources against the same roots.
    {
        std::vector<std::filesystem::path> sourceRoots;
        for (const std::string& root : contentRoots)
            sourceRoots.emplace_back(root);
        Workspace->World.SetContentRoots(std::move(sourceRoots));
    }
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
        *Materials, std::move(contentRoots), Workspace->Selection, Workspace->MeshEdit);
    Files->RegisterCommands(engine.Console().Registry());
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
    RenderFeature = engine.Graphics().MainRenderer.StageFeature(
        std::move(renderFeature),
        FeatureRegistration{ .Id = kEditorRenderFeatureId,
                             .DependsOn = kEditorRenderDependsOn });
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
        .LeftEdge = 0.06f,
        .Left = 0.16f,
        .Right = 0.3f,
        .CenterBottom = 0.35f,
        .RightBottom = 0.3f,
    };
    auto uiFeature = std::make_unique<EditorUiFeature>(engine, *Window, instance, frames,
                                                       "kyusu.imgui.ini", layoutRatios);
    // Provisional, for the panel and chrome wiring below; reassigned from what
    // AddFeature returns once the feature is actually registered.
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
    // The shell's nameplate and its readout of what is open. Product names
    // are data here, as on the window title.
    UiFeature->SetIdentity(ShellIdentity{
        .Product = "KYUSU",
        .LogoPath = std::string(SENCHA_EDITOR_BRAND_DIR) + "/kyusu-logo.svg",
    });
    UiFeature->SetStatusProvider([this]() { return Files ? Files->DocumentLabel() : std::string{}; });

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
    TopBar = std::make_unique<WorkspaceBar>();
    TopBar->SetPlayControls({
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
            std::vector<WorkspaceBar::PlayControls::ProfileChoice> choices;
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
    UiFeature->AddPanel(std::make_unique<ToolPalettePanel>([this] { return Workspace->Interaction.Tools.get(); }));
    Toolbar->SetSurfaceProvider([this] { return UiFeature->SurfaceFor(BarRole::Toolbar); });
    // The workspace bar sits under the caption as app chrome, on the primary
    // viewport's header plate, so it reads apart from the editing row.
    UiFeature->AddChrome([this] { TopBar->Draw(); });
    UiFeature->AddChrome([this] { StatusBar->Draw(); });
    UiFeature->AddOverlay([this]
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
        UiFeature->AddPanel(std::move(perspectivePanel));
        auto orthoPanel = std::make_unique<ViewportPanel>(
            Workspace->Layout, Workspace->Interaction.Marquee, Workspace->Interaction.Overlay,
            RenderFeature->GetViewportTargets(), "ORTHO", DockSlot::CenterBottom, 1.0f,
            PanelStyle::Viewport, PanelPersistence{ "ortho", PanelVisibilityPolicy::Remembered }, orthoId);
        orthoPanel->SetSceneDropHandler(placeDroppedScene);
        OrthoPanel = orthoPanel.get();
        UiFeature->AddPanel(std::move(orthoPanel));
    }
    else
    {
        std::fprintf(stderr, "[editor] no viewport render feature; "
                             "viewport panels are unavailable\n");
    }
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
        UiFeature->AddPanel(std::make_unique<SceneBrowserPanel>(
            Workspace->World, Workspace->Selection, *Commands,
            std::move(sceneRoots), std::move(thumbnails)));
    }
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
    // Same reason as the viewport panels: the shadow readout is the render
    // feature's own state, so there is nothing to show without it.
    if (RenderFeature != nullptr)
    {
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
    }
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
        Assets->Assets, *Assets->Textures, engine.Graphics().Images, engine.Graphics().Samplers,
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

    renderer.StageFeature(std::move(uiFeature),
                          FeatureRegistration{ .Id = kEditorUiFeatureId });

    // Both features are staged now, so the batch can resolve. Setup runs here,
    // in dependency order, and a feature that fails takes its dependents and
    // the pointers cached against them with it. EditorUiFeature has real
    // failure paths -- no graphics queue family, descriptor pool, SDL or Vulkan
    // backend init -- and it owns the panels below.
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed))
    {
        // The renderer has already named the graph problem per offending id.
        // What matters here is that a refused batch registers nothing, so the
        // list names every staged id and the clauses below null all of them.
        std::fprintf(stderr, "[editor] render feature batch was refused; "
                             "the editor runs without its own features\n");
    }
    const auto didFail = [&failed](std::string_view id)
    {
        return std::find(failed.begin(), failed.end(), id) != failed.end();
    };
    if (didFail(kEditorUiFeatureId))
    {
        std::fprintf(stderr, "[editor] UI feature failed to set up; "
                             "editor panels are unavailable\n");
        UiFeature = nullptr;
        PerspectivePanel = nullptr;
        OrthoPanel = nullptr;
        ConsolePanel = nullptr;
        Browser = nullptr;
    }
    if (didFail(kEditorRenderFeatureId))
    {
        std::fprintf(stderr, "[editor] viewport render feature failed to set up; "
                             "viewports will not draw\n");
        RenderFeature = nullptr;
    }
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
        : Importers(jobs)
    {
    }

    struct RootWatch
    {
        AssetSourceWatcher Watcher;
        AssetHotReloader Reloader;
    };

    ContentImporterSet Importers;
    std::vector<std::unique_ptr<RootWatch>> Roots;
    std::chrono::steady_clock::time_point NextPoll{};
};

void EditorServices::BuildAuthoredWorkflows()
{
    UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr;
    if (ui == nullptr || !ui->IsReady() || !Project.has_value())
        return;

    // The window, as authored UI sees it. One surface for every authored screen
    // Kyusu opens, because focus and modality are arbitrated within a surface:
    // a dialog on a surface of its own would take focus from nothing. Tracked
    // against the window in ProcessFrame, since a retained document re-flows on
    // a resize where a baked atlas cannot.
    if (!AuthoredSurface.IsValid() && Window != nullptr)
    {
        AuthoredSurface = ui->CreateSurface(
            "kyusu",
            RenderExtent{ Window->GetExtent().Width, Window->GetExtent().Height });
    }
    if (!AuthoredSurface.IsValid())
        return;

    ProfilesModal = std::make_unique<CookProfilesModal>(*ui, AuthoredSurface, &*Project);
    if (Workspace != nullptr && Commands != nullptr)
    {
        Inspector = std::make_unique<InspectorSurface>(
            *ui, AuthoredSurface, Workspace->World, Workspace->Selection, *Commands);
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

void EditorServices::BuildSourceWatch()
{
    if (!Project || !Assets || Project->ContentRoots.empty())
        return;

    Engine& engine = *EnginePtr;
    SourceWatch = std::make_unique<SourceWatchState>(&engine.Jobs());

#if defined(SENCHA_ENABLE_UI) && defined(SENCHA_EDITOR_UI_DIR)
    // The editor's own authored UI, watched against the ENGINE's asset stack --
    // the one it was mounted into, and the one Engine::Ui() resolves through.
    // This is what makes editing Kyusu's own interface a save-and-look loop
    // rather than a restart.
    {
        auto watch = std::unique_ptr<SourceWatchState::RootWatch>(new SourceWatchState::RootWatch{
            AssetSourceWatcher(engine.Logging(), SENCHA_EDITOR_UI_DIR,
                               { ".rml", ".rcss", ".ttf", ".otf" }),
            AssetHotReloader(engine.Logging(), engine.Content().Assets().Assets,
                             engine.Content().Assets().Registry,
                             SourceWatch->Importers.Registry(), engine.Tasks(),
                             SENCHA_EDITOR_UI_DIR),
        });
        watch->Watcher.Initialize();
        SourceWatch->Roots.push_back(std::move(watch));
    }
#endif

    for (const std::string& root : Project->ContentRoots)
    {
        auto watch = std::unique_ptr<SourceWatchState::RootWatch>(new SourceWatchState::RootWatch{
            AssetSourceWatcher(engine.Logging(), root,
                               { ".smat", ".png", ".meta", ".rml", ".rcss", ".ttf", ".otf" }),
            AssetHotReloader(engine.Logging(), Assets->Assets, Assets->Registry,
                             SourceWatch->Importers.Registry(), engine.Tasks(), root),
        });
        watch->Watcher.Initialize();
        SourceWatch->Roots.push_back(std::move(watch));
    }
}

void EditorServices::DrawRadialMenu(const RadialMenuSession& wheel, const IRadialMenuModel& menu)
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

void EditorServices::ProcessFrame()
{
    // Before the engine updates the UI: act on what the document asked for and
    // publish what it should now show, so a click and its answer land in the
    // same frame. This hook runs inside FramePhase::Update, which is where the
    // engine guarantees that ordering.
    // A retained document re-flows on a resize, so the surface follows the
    // window rather than latching whatever size it was created at. Unchanged
    // sizes cost a comparison.
    if (AuthoredSurface.IsValid() && Window != nullptr)
    {
        if (UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr; ui != nullptr)
        {
            ui->SetSurfaceSize(AuthoredSurface,
                               RenderExtent{ Window->GetExtent().Width,
                                             Window->GetExtent().Height });
        }
    }

    if (ProfilesModal != nullptr)
        ProfilesModal->Update();
    if (Inspector != nullptr)
        Inspector->Update();

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
    // No World and no session here, so the registrar carries neither -- a game
    // component that only replicates is registered and simply has nowhere to go.
    ComponentRegistrar registrar(nullptr, &EditorSceneSerializers(), nullptr);
    GameModule.Instance->OnRegisterComponents(registrar);
    const std::span<const ComponentTypeId> added = registrar.AddedSerializers();
    GameModuleSerializerTypes.assign(added.begin(), added.end());

    // Storage is what a game component needs to exist in a document; its
    // gameplay vocabulary is what the names inside authored content resolve
    // against. Each document installs it into its own World, so this is held
    // rather than run once.
    SetEditorModuleVocabulary([game = GameModule.Instance](World& world)
                              { game->OnRegisterVocabulary(world); });
    std::fprintf(stderr, "[editor] loaded game module '%s'\n", modulePath.c_str());
}

void EditorServices::InitAssets()
{
    if (EnginePtr == nullptr)
        return;
    Engine& engine = *EnginePtr;
    GraphicsServices& graphics = engine.Graphics();
    LoggingProvider& logging = engine.Logging();

    Assets.emplace(logging, graphics.Buffers, graphics.Images, graphics.Descriptors,
                   graphics.Samplers, engine.SceneSerializers());
    if (!Project)
        return;

    MountProjectContent(*Project, *Assets, logging, &engine.Jobs());
#ifdef SENCHA_ENABLE_UI
#ifdef SENCHA_EDITOR_UI_DIR
    // Into the ENGINE's asset stack, not the editor's. The editor keeps its own
    // RuntimeAssets for project content, but Engine::Ui() resolves a package
    // through the engine's -- so authored UI mounted anywhere else is authored
    // UI the UI service cannot find.
    MountEditorContent(SENCHA_EDITOR_UI_DIR, engine.Content().Assets(), logging, &engine.Jobs());
#endif
#endif
}

void EditorServices::UnloadGameModule()
{
    if (!GameModule.IsValid())
        return;

    // Retract the serializers while the module is still mapped, then unmap. The
    // vocabulary installer goes with them: its target is code in the module's
    // image, so it must not outlive the mapping. Documents already built keep
    // the names they were given -- they are values in their own worlds.
    for (ComponentTypeId type : GameModuleSerializerTypes)
        (void)EditorSceneSerializers().Remove(type);
    GameModuleSerializerTypes.clear();
    SetEditorModuleVocabulary({});
    ModuleLoader.Unload(GameModule);
}
