#include "LevelWorkspace.h"

#include "actions/ActionCatalog.h"

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
#include "EditorCookRuntime.h"
#include "project/MaterialLibrary.h"
#include "document/commands/BakeBrushToMeshCommand.h"
#include "export/GltfMeshExport.h"

#include <world/ComponentRegistrar.h>
#include "render/EditorRenderFeature.h"
#include "ui/CookProfilesModal.h"
#include <ui/UiService.h>
#include "ui/EditorUiFeature.h"
#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeControls.h"
#include "document/commands/SceneInstanceCommands.h"
#include "render/RenderFeatureDetach.h"
#include "documents/DocumentSourceSet.h"
#include "ui/MaterialThumbnailCache.h"
#include "render/SceneThumbnailCache.h"
#include "actions/ActionOffer.h"
#include "resources/ResourceActions.h"
#include "authoring/ComponentPropertySource.h"
#include "workspace/ElementVerbs.h"
#include "workspace/FaceUvSource.h"
#include "workspace/MaterialExplorer.h"
#include "workspace/PrefabExplorer.h"
#include "workspace/EntityVerbs.h"
#include "workspace/LightingDiagnostics.h"
#include "workspace/PartitionOutline.h"
#include "workspace/WorldExplorer.h"
#include "scene_source/SceneSourcePaths.h"
#include "workspace/SelectionSummary.h"
#include "workspace/ManipulationActions.h"
#include "tools/ToolParameterSource.h"
#include "ui/RadialMenuScreen.h"
#include "ui/LevelViewSurface.h"
#include "workspaces/WorkspaceWindow.h"

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
constexpr std::string_view kArrangementCVar = "editor.view.arrangement";
constexpr std::string_view kDividerXCVar = "editor.view.divider_x";
constexpr std::string_view kDividerYCVar = "editor.view.divider_y";
} // namespace

LevelWorkspace::LevelWorkspace(Engine& engine,
                               SdlWindow& window,
                               ProjectDescriptor* project,
                               Game* module,
                               MaterialLibrary& materials,
                               DocumentSourceSet& documents,
                               ActionCatalog& actions, PromptQueue& prompts, ProblemList& problems)
    : Documents(documents)
    , Prompts(prompts.MakeAsker())
    , Problems(problems)
    , Catalog(actions)
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
    DefineTransportActions();
}

LevelWorkspace::~LevelWorkspace()
{
    if (Window != nullptr)
        SetRelativeMouseMode(*Window, false);
    // Its problems go with it.
    Problems.Clear("world");
    Problems.Clear("lighting");

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
    WheelScreen.reset();
    ViewSurface.reset();
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
    // The thumbnail bindings release texture refs through the asset stack and
    // free ImGui descriptor sets (the panels referencing the cache never touch
    // it in their destructors).
    Thumbnails.reset();
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

    // The workspace's operations: defined once in the catalog, performed by
    // this workspace's handlers while it is the one in use. Gizmo switches carry
    // Shift to stay off the fly camera's bare W/A/S/D + Q/E. Escape reaches its
    // handler only when no drag is in flight (the viewport dispatcher consumes
    // it to cancel one), so it climbs the editing context one level per press.
    struct LevelAction
    {
        std::string_view Id;
        std::string_view Label;
        std::string_view Category;
        KeyChord Chord;
        std::function<void()> Invoke;
    };
    const LevelAction actions[] = {
        { "edit.delete",           "Delete",                      "Edit",     { SDLK_DELETE, {} },                        [this] { Workspace->DeleteSelection(); } },
        { "edit.dissolve",         "Dissolve Edges",              "Edit",     { SDLK_BACKSPACE, {} },                     [this] { Workspace->DissolveSelectedEdges(); } },
        { "edit.select_all",       "Select All",                  "Edit",     { SDLK_A, { .Ctrl = true } },               [this] { Workspace->SelectAll(); } },
        { "edit.duplicate",        "Duplicate",                   "Edit",     { SDLK_D, { .Ctrl = true } },               [this] { Workspace->Actions.Duplicate(/*asInstance*/ false); } },
        { "edit.duplicate_instance", "Duplicate as Instance",     "Edit",     { SDLK_D, { .Alt = true } },                [this] { Workspace->Actions.Duplicate(/*asInstance*/ true); } },
        { "edit.repeat",           "Repeat Last",                 "Edit",     { SDLK_R, { .Ctrl = true } },               [this] { Workspace->Actions.RepeatLast(); } },
        { "edit.escape",           "Step Back",                   "Edit",     { SDLK_ESCAPE, {} },                        [this] { Workspace->EscapeStep(); } },
        { "file.new",              "New Level",                   "File",     { SDLK_N, { .Ctrl = true } },               [this] { AfterSettlingChanges([this] { Files->New(); }); } },
        { "file.open",             "Open...",                     "File",     { SDLK_O, { .Ctrl = true } },               [this] { AfterSettlingChanges([this] { Files->RequestOpen(); }); } },
        { "material.apply",        "Apply Material",              "Material", { SDLK_T, { .Shift = true } },              [this] { Workspace->ApplyActiveMaterialToSelectedFaces(); } },
        { "material.copy_proj",    "Copy Face Projection",        "Material", { SDLK_C, { .Ctrl = true, .Shift = true } }, [this] { Workspace->CopySelectedFaceProjection(); } },
        { "material.paste_proj",   "Paste Face Projection",       "Material", { SDLK_V, { .Ctrl = true, .Shift = true } }, [this] { Workspace->PasteFaceProjectionToSelection(); } },
    };
    const auto offer = [&](std::string_view id, std::string_view label, std::string_view category,
                           std::vector<KeyChord> chords, IconId icon, std::function<void()> invoke)
    {
        OfferAction(Catalog, ActionHandlers,
                    { .Id = std::string(id), .Label = std::string(label), .Category = std::string(category),
                      .Icon = icon, .DefaultChords = std::move(chords) },
                    { .Invoke = std::move(invoke) });
    };
    for (const LevelAction& action : actions)
        offer(action.Id, action.Label, action.Category, { action.Chord }, IconId::None, action.Invoke);
    OfferManipulationActions(Catalog, ActionHandlers, *Workspace);
    Verbs = std::make_unique<ElementVerbs>(*Workspace, [this] { ExportSelectionGlb(); });
    Verbs->Offer(Catalog, ActionHandlers);
    Uv = std::make_unique<FaceUvSource>(*Workspace);
    Uv->Offer(Catalog, ActionHandlers);
    Components = std::make_unique<ComponentPropertySource>(Workspace->World, Workspace->Selection, *Commands,
                                                           Workspace->Affordances->Registry());
    Entities = std::make_unique<EntityVerbs>(*Workspace, [this](const std::string& source) {
        return Files != nullptr && Files->OpenSceneSource(source);
    });
    Entities->Offer(Catalog, ActionHandlers);
    Entities->OfferZoneMoves(Catalog, ActionHandlers);
    WorldOutline = std::make_unique<WorldExplorer>(*Workspace);
    Partition = std::make_unique<PartitionOutline>(*Workspace);
    OfferAction(Catalog, ActionHandlers,
                { .Id = "explorer.zones", .Label = "Zones", .Category = "Explore",
                  .DefaultChords = { { SDLK_O, { .Shift = true } } } },
                {});
    OfferAction(Catalog, ActionHandlers,
                { .Id = "explorer.world", .Label = "World", .Category = "Explore",
                  .DefaultChords = { { SDLK_O, {} } } },
                {});
    // The explorers: defined here, opened by whichever shell shows the workspace.
    OfferAction(Catalog, ActionHandlers, MaterialExplorerAction(), {});
    OfferAction(Catalog, ActionHandlers,
                { .Id = "explorer.prefabs", .Label = "Prefabs", .Category = "Explore",
                  .Icon = IconId::Box, .DefaultChords = { { SDLK_P, { .Shift = true } } } },
                {});
    offer("prefabs.rescan", "Rescan Prefabs", "Explore", {}, IconId::Refresh, [this] {
        if (PrefabsExplorer == nullptr)
            return;
        std::vector<std::filesystem::path> roots;
        if (Project)
            for (const std::string& root : Project->ContentRoots)
                roots.emplace_back(root);
        PrefabsExplorer->Rescan(roots);
        // A saved scene may have changed: its old picture is stale.
        if (SceneThumbnailCache* cache = RenderFeature != nullptr ? RenderFeature->SceneThumbnails() : nullptr)
            cache->Clear();
    });
    OfferAction(Catalog, ActionHandlers, MaterialRescanAction(), { .Invoke = [this] {
                    if (Materials == nullptr)
                        return;
                    Materials->Rescan(Materials->Roots());
                    // A rescan may follow a save that re-pointed a material's textures.
                    if (Thumbnails != nullptr)
                        Thumbnails->Clear();
                } });
    OfferResourceActions(Catalog, ActionHandlers, Workspace->Resources, [this] { return HeldType(); });

    // How the views share the content area, and which one a single view shows.
    struct ArrangementRow
    {
        const char* Id;
        const char* Label;
        SDL_Keycode Key;
        ViewportArrangementKind Kind;
    };
    for (const ArrangementRow& row : { ArrangementRow{ "view.one", "One View", SDLK_1, ViewportArrangementKind::Single },
                                       ArrangementRow{ "view.two", "Two Views", SDLK_2, ViewportArrangementKind::Split },
                                       ArrangementRow{ "view.four", "Four Views", SDLK_4, ViewportArrangementKind::Quad } })
        OfferAction(Catalog, ActionHandlers,
                    { .Id = row.Id, .Label = row.Label, .Category = "View", .DefaultChords = { { row.Key, { .Alt = true } } } },
                    { .Invoke = [this, kind = row.Kind] { SetArrangement(kind); },
                      .Checked = [this, kind = row.Kind] { return Arrangement() == kind; } });
    struct ProjectionRow
    {
        const char* Id;
        const char* Label;
        SDL_Keycode Key;
        ViewportOrientation Orientation;
    };
    for (const ProjectionRow& row : { ProjectionRow{ "view.perspective", "Perspective View", SDLK_KP_5, ViewportOrientation::Perspective },
                                      ProjectionRow{ "view.top", "Top View", SDLK_KP_7, ViewportOrientation::Top },
                                      ProjectionRow{ "view.front", "Front View", SDLK_KP_1, ViewportOrientation::Front },
                                      ProjectionRow{ "view.side", "Side View", SDLK_KP_3, ViewportOrientation::Right } })
        OfferAction(Catalog, ActionHandlers,
                    { .Id = row.Id, .Label = row.Label, .Category = "View", .DefaultChords = { { row.Key, {} } } },
                    { .Invoke = [this, orientation = row.Orientation] { ShowView(orientation); },
                      .Checked = [this, orientation = row.Orientation] {
                          const EditorViewport* active = Workspace->Layout.Active();
                          return active != nullptr && active->Orientation == orientation;
                      } });

    // Tool activation is generated from the registry rather than listed above: a
    // tool declares its own key, so adding one needs no edit here. The action is
    // "tool.<id>", which is also what a keymap file overrides, and the registry
    // resolves at fire time because the workspace owns it.
    if (ToolRegistry* tools = Workspace->Interaction.Tools.get())
    {
        for (const std::unique_ptr<ITool>& tool : tools->GetTools())
        {
            if (tool == nullptr)
                continue;
            const std::string id(tool->GetId());
            const ITool::Shortcut shortcut = tool->GetShortcut();
            std::vector<KeyChord> chords;
            if (shortcut.Key != SDLK_UNKNOWN)
                chords.push_back(KeyChord{ shortcut.Key, shortcut.Mods });
            offer("tool." + id, tool->GetDisplayName(), "Tool", std::move(chords), tool->GetIcon(),
                  [this, id]
                  {
                      if (ToolRegistry* live = Workspace->Interaction.Tools.get())
                          (void)live->Activate(id);
                  });
        }
    }

    // A wheel's key is held rather than pressed, so the session owns both
    // edges of it and no handler claims the action; it is defined so the
    // keymap, hints and palette name it like any other.
    offer("tool.wheel", "Tool Wheel", "Tool", { KeyChord{ SDLK_Q, {} } }, IconId::None, {});
    offer("gizmo.wheel", "Transform Wheel", "Transform", { KeyChord{ SDLK_Z, {} } }, IconId::None, {});

    // A drafted operation's Apply and Cancel, for whatever presents them. The
    // tools take Enter and Escape themselves, so these carry no chord.
    const auto activeTool = [this]() -> std::pair<ITool*, ToolRegistry*> {
        ToolRegistry* tools = Workspace->Interaction.Tools.get();
        return { tools != nullptr ? tools->GetActiveTool() : nullptr, tools };
    };
    offer("tool.apply", "Apply", "Tool", {}, IconId::Check, [activeTool] {
        if (const auto [tool, tools] = activeTool(); tool != nullptr && tool->Draft() == ToolDraft::Ready)
            tool->CommitPending(tools->GetContext());
    });
    offer("tool.cancel", "Cancel", "Tool", {}, IconId::Cancel, [activeTool] {
        if (const auto [tool, tools] = activeTool(); tool != nullptr && tool->Draft() != ToolDraft::None)
            tool->OnCancel(tools->GetContext());
    });
    ToolParameters = std::make_unique<ToolParameterSource>([this] { return Workspace->Interaction.Tools.get(); });

    // A wheel's key is held, not pressed, so it is not a shortcut row: the
    // session owns both edges of it. The action names are what a keymap file
    // rebinds, like any other. Both wheels place themselves against the same
    // window and open over the same scene test.
    {
        const auto chordFor = [&](std::string_view action, SDL_Keycode fallback)
        {
            const std::span<const KeyChord> chords = Catalog.Chords(action);
            return chords.empty() ? KeyChord{ .Key = fallback, .Mods = {} } : chords.front();
        };
        const auto frame = [this]
        {
            // The one place a wheel learns the window and the UI scale:
            // captured together at open, resolved once into its layout.
            const WindowExtent extent = Window->GetExtent();
            return RadialMenu::Frame{
                .Scale = EditorUi::UiScale,
                .Min = {},
                .Max = { static_cast<float>(extent.Width), static_cast<float>(extent.Height) },
            };
        };
        const auto pointerOnScene = [this](Vec2d pointer)
        {
            // The view whose rect holds the pointer, with nothing of the
            // window's UI over it. Which view is active or was focused last
            // plays no part.
            return Workspace->Layout.ResolveAt(pointer).IsValid() && !WindowUiCapture().Mouse;
        };
        ToolMenu = std::make_unique<ToolRegistryMenuModel>(*Workspace->Interaction.Tools);
        ToolWheel = std::make_unique<RadialMenuSession>(*ToolMenu, chordFor("tool.wheel", SDLK_Q), frame, pointerOnScene);
        GizmoMenu = std::make_unique<TransformModeMenuModel>([this] { return Workspace->Interaction.Manipulators; });
        GizmoWheel = std::make_unique<RadialMenuSession>(*GizmoMenu, chordFor("gizmo.wheel", SDLK_Z), frame, pointerOnScene);
    }

    Router = std::make_unique<InputRouter>();
    // The window's UI is the top layer of the input stack: events it wants are
    // consumed here before navigation, tools, or shortcuts can act on them.
    // (The guard adds pointer capture so drags survive crossing the UI.)
    Router->AddHandler(MakeUiInputGuard([this] { return WindowUiCapture(); }));
    // The wheels sit under the guard (a focused text field keeps its letters)
    // and above everything else: an open wheel owns the pointer and the keys,
    // which is also what keeps the other wheel closed, and each yields its key
    // to any gesture already holding the pointer.
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return ToolWheel->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return GizmoWheel->OnInput(e, cap); });
    DividerDrag = std::make_unique<ViewDividerDrag>(
        [this] {
            return ViewDividerDrag::Arrangement{ .Kind = Arrangement(),
                                                 .Content = ContentRect,
                                                 .Divider = Divider() };
        },
        [this](Vec2d divider) { SetDivider(divider); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return DividerDrag->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Navigation->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Workspace->Interaction.Dispatcher->OnInput(e, cap); });

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

    console.Registry().RegisterCVar({
        .Name = std::string(kArrangementCVar),
        .Owner = "editor",
        .Type = CVarType::Int,
        .DefaultValue = std::int64_t{ 0 },
        .CurrentValue = std::int64_t{ 0 },
        .Flags = CVarFlags::Archive,
        .Help = "Level views: 0 one view, 1 two side by side, 2 four in a grid.",
        .Source = { "editor" },
        .Min = 0.0,
        .Max = 2.0,
    });

    for (const auto& [name, help] : { std::pair{ kDividerXCVar, "Level views: where side-by-side views meet, as a fraction of the width." },
                                      std::pair{ kDividerYCVar, "Level views: where the rows of four views meet, as a fraction of the height." } })
        console.Registry().RegisterCVar({
            .Name = std::string(name),
            .Owner = "editor",
            .Type = CVarType::Double,
            .DefaultValue = 0.5,
            .CurrentValue = 0.5,
            .Flags = CVarFlags::Archive,
            .Help = help,
            .Source = { "editor" },
            .Min = 0.15,
            .Max = 0.85,
        });

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

    Surface.File = WorkspaceFileActions{
        .New = [this] { AfterSettlingChanges([this] { Files->New(); }); },
        .NewWorld = [this] { AfterSettlingChanges([this] { Files->NewWorld(); }); },
        .Open = [this] { AfterSettlingChanges([this] { Files->RequestOpen(); }); },
        .Save = [this] { Files->Save(); },
        .SaveAs = [this] { Files->RequestSaveAs(); },
    };


    // The views show what the render feature draws, and staging hands back
    // nothing when the renderer never came up: an editor with no views is what
    // a dead renderer means anyway.
    if (RenderFeature != nullptr)
    {
        if (UiService* ui = EnginePtr->TryUi(); ui != nullptr && ui->IsReady())
            ViewSurface = std::make_unique<LevelViewSurface>(*ui, Workspace->Layout, Workspace->Interaction.Overlay,
                                                             Workspace->Interaction.Marquee, RenderFeature);
        if (UiService* ui = EnginePtr->TryUi(); ui != nullptr && ui->IsReady())
            WheelScreen = std::make_unique<RadialMenuScreen>(*ui);
    }
    else
    {
        std::fprintf(stderr, "[editor] no viewport render feature; the views are unavailable\n");
    }
    {
        std::vector<std::filesystem::path> sceneRoots;
        if (Project)
            for (const std::string& root : Project->ContentRoots)
                sceneRoots.emplace_back(root);
        if (SceneThumbnailCache* cache = RenderFeature != nullptr ? RenderFeature->SceneThumbnails() : nullptr)
        {
            UiService* ui = engine.TryUi();
            cache->SetContentRoots(sceneRoots);
            cache->SetUi(ui != nullptr && ui->IsReady() ? ui : nullptr);
        }
        PrefabsExplorer = std::make_unique<PrefabExplorer>(*Workspace, [this](const std::string& path) {
            SceneThumbnailCache* cache = RenderFeature != nullptr ? RenderFeature->SceneThumbnails() : nullptr;
            return cache != nullptr ? cache->ImageName(path) : std::string{};
        });
        PrefabsExplorer->Rescan(sceneRoots);
    }
    const auto previewBuilder = [this]() -> SceneRenderQueueBuilder* {
        return RenderFeature != nullptr ? RenderFeature->FocusQueueBuilder() : nullptr;
    };
    const auto lastCook = [this]() -> const CookSession::Record* {
        return CookRuntime != nullptr ? CookRuntime->LastRecord() : nullptr;
    };
    LightingReports lighting{
        .Shadows = RenderFeature != nullptr ? &RenderFeature->ShadowReadout() : nullptr,
        .Preview = [previewBuilder, lastCook] {
            const SceneRenderQueueBuilder* builder = previewBuilder();
            if (builder == nullptr || lastCook() == nullptr)
                return BakedLightingPreview::Unavailable;
            if (!builder->LightmapPreviewEnabled() || !builder->LightmapPreviewLoaded())
                return BakedLightingPreview::Off;
            return builder->LightmapPreviewStale() ? BakedLightingPreview::Stale : BakedLightingPreview::Fresh;
        },
        .ShowPreview = [this, previewBuilder](bool enabled) {
            SceneRenderQueueBuilder* builder = previewBuilder();
            if (builder == nullptr)
                return;
            if (enabled && CookRuntime != nullptr)
                CookRuntime->RefreshPreviewNow(*builder);
            builder->SetLightmapPreviewEnabled(enabled);
        },
        .RerenderShadows = [this] {
            if (RenderFeature != nullptr)
                RenderFeature->InvalidateShadows();
        },
        .LastCook = [lastCook]() -> std::optional<LightingCook> {
            const CookSession::Record* record = lastCook();
            if (record == nullptr)
                return std::nullopt;
            return LightingCook{ .ProbeVolumes = record->ProbeVolumeCount, .Probes = record->ProbeCount };
        },
    };
    Lighting = std::make_unique<LightingDiagnostics>(*Workspace, *Commands, std::move(lighting));
    Lighting->Offer(Catalog, ActionHandlers);
    // Material picking surfaces: thumbnail residency for both panels, bounded
    // by the budget cvar so a large library never pins every base color
    // texture on the GPU.
    MaterialThumbnailCache::RegisterBudget(console.Registry());
    UiService* ui = engine.TryUi();
    Thumbnails = std::make_unique<MaterialThumbnailCache>(
        Assets->Assets, *Assets->Textures,
        Assets->Registry, ui != nullptr && ui->IsReady() ? ui : nullptr,
        &engine.Graphics().MainRenderer.GetUiTextures());
    MaterialsExplorer = std::make_unique<MaterialExplorer>(
        *Materials, *Workspace, [this](const std::string& path) { return Thumbnails->ImageName(path); });

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
    AskAboutChanges(Prompts, !changed.empty(), LevelDocument->DocumentLabel(LevelDocumentSource::kKey),
                    [this, proceed = std::move(proceed)](DirtyDisposition disposition) {
                        if (disposition == DirtyDisposition::Save)
                        {
                            // A save that did not write stops here; its follow-up says why.
                            const DocumentSaveResult saved = Documents.Save(LevelDocument->Ref());
                            if (saved.Status != DocumentSaveStatus::Saved
                                && saved.Status != DocumentSaveStatus::SavedWithProblems)
                                return;
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

ViewportArrangementKind LevelWorkspace::Arrangement() const
{
    std::int64_t value = 0;
    if (const CVarMetadata* var = EnginePtr->Console().Registry().FindCVar(kArrangementCVar))
        if (const std::int64_t* stored = std::get_if<std::int64_t>(&var->CurrentValue))
            value = *stored;
    switch (value)
    {
    case 1:  return ViewportArrangementKind::Split;
    case 2:  return ViewportArrangementKind::Quad;
    default: return ViewportArrangementKind::Single;
    }
}

Vec2d LevelWorkspace::Divider() const
{
    const auto read = [this](std::string_view name) {
        const CVarMetadata* var = EnginePtr->Console().Registry().FindCVar(name);
        const double* value = var != nullptr ? std::get_if<double>(&var->CurrentValue) : nullptr;
        return value != nullptr ? static_cast<float>(*value) : 0.5f;
    };
    return Vec2d(read(kDividerXCVar), read(kDividerYCVar));
}

void LevelWorkspace::SetDivider(Vec2d divider)
{
    ConsoleRegistry& registry = EnginePtr->Console().Registry();
    const ConsoleValueSource source{ .Description = "view divider" };
    (void)registry.SetCVar(kDividerXCVar, static_cast<double>(divider.X), source, ConsolePhase::GameplayStarted);
    (void)registry.SetCVar(kDividerYCVar, static_cast<double>(divider.Y), source, ConsolePhase::GameplayStarted);
}

void LevelWorkspace::SetArrangement(ViewportArrangementKind kind)
{
    const std::int64_t value = kind == ViewportArrangementKind::Split ? 1 : kind == ViewportArrangementKind::Quad ? 2 : 0;
    (void)EnginePtr->Console().Registry().SetCVar(kArrangementCVar, value,
                                                  ConsoleValueSource{ .Description = "view action" },
                                                  ConsolePhase::GameplayStarted);
}

void LevelWorkspace::ShowView(ViewportOrientation orientation)
{
    for (const ViewportLayout::ViewportStorage& viewport : Workspace->Layout.All())
    {
        if (viewport != nullptr && viewport->Orientation == orientation)
        {
            Workspace->Layout.SetActive(viewport->Id);
            return;
        }
    }
}

void LevelWorkspace::MoveTo(const WorkspaceWindow& window)
{
    if (ViewSurface != nullptr)
        ViewSurface->MoveTo(window.Content);
    if (WheelScreen != nullptr)
        WheelScreen->MoveTo(window.Overlay);
    if (ProfilesModal != nullptr)
        ProfilesModal->MoveTo(window.Interactive);
}

void LevelWorkspace::SetVisible(bool visible)
{
    if (Visible == visible)
        return;
    Visible = visible;
    if (ViewSurface != nullptr)
        ViewSurface->SetShown(visible);
    if (WheelScreen != nullptr)
        WheelScreen->SetShown(visible);
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
    ProfilesModal = std::make_unique<CookProfilesModal>(*ui, *Project);
}

void LevelWorkspace::ProcessFrame()
{
    // Before the engine updates the UI: act on what the document asked for and
    // publish what it should now show, so a click and its answer land in the
    // same frame. This hook runs inside FramePhase::Update, which is where the
    // engine guarantees that ordering.
    if (ProfilesModal != nullptr)
    {
        ProfilesModal->Update();
        if (ProfilesModal->Editor().Revision() != OfferedProfilesRevision)
        {
            OfferProfileActions();
            OfferedProfilesRevision = ProfilesModal->Editor().Revision();
        }
    }

    if (Entities != nullptr)
        Entities->OfferZoneMoves(Catalog, ActionHandlers);
    PublishProblems();
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

    // Every view's rect, target size and image for this frame, from where the
    // window last said the content shows; a view not shown loses its rect, so
    // ResolveAt never routes input to it.
    if (ViewSurface != nullptr)
        ViewSurface->Arrange(Visible ? ContentRect : Rect2d{}, Arrangement(),
                             Divider(), EditorUi::UiScale);
    // Only one wheel is ever open: each yields its key to the other.
    if (WheelScreen != nullptr)
    {
        if (ToolWheel->GetPhase() == RadialMenuPhase::Open)
            WheelScreen->Present(ToolWheel.get(), ToolMenu.get());
        else if (GizmoWheel->GetPhase() == RadialMenuPhase::Open)
            WheelScreen->Present(GizmoWheel.get(), GizmoMenu.get());
        else
            WheelScreen->Present(nullptr, nullptr);
    }

    // Only what an explorer showed recently stays on the GPU.
    if (Thumbnails)
        Thumbnails->UpdateFrame(*EnginePtr);
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

void LevelWorkspace::DefineTransportActions()
{
    // The transport as actions: the rail shows the keys, the console and the
    // palette reach the same handlers. A cook reads the live documents (and
    // force-saves the world first), so open previews settle before it starts or
    // they would cook half-staged.
    const auto offer = [this](std::string id, std::string label, ActionHandler handler) {
        OfferAction(Catalog, ActionHandlers,
                    { .Id = std::move(id), .Label = std::move(label), .Category = "Transport" }, std::move(handler));
    };
    const auto cooking = [this] { return CookRuntime != nullptr && CookRuntime->IsActive(); };
    const auto playing = [this] { return CookRuntime != nullptr && CookRuntime->Player().IsPlaying(); };
    offer("transport.cook", "Cook", {
        .Invoke = [this] { Workspace->ResolvePendingEdits(); if (CookRuntime) CookRuntime->Start(); },
        .Enabled = [this, cooking] { return CookRuntime != nullptr && !cooking(); } });
    offer("transport.rebuild", "Cook From Scratch", {
        .Invoke = [this] { Workspace->ResolvePendingEdits(); if (CookRuntime) CookRuntime->Start(/*rebuild*/ true); },
        .Enabled = [this, cooking] { return CookRuntime != nullptr && !cooking(); } });
    offer("transport.cancel", "Cancel Cook", {
        .Invoke = [this] { if (CookRuntime) CookRuntime->Cancel(); },
        .Enabled = cooking });
    offer("transport.play", "Play", {
        .Invoke = [this] { if (CookRuntime) CookRuntime->Player().Play(CookRuntime->Player().LastCookedMap()); },
        .Enabled = [this, playing] { return CookRuntime != nullptr && !playing(); } });
    offer("transport.stop", "Stop", { .Invoke = [this] { if (CookRuntime) CookRuntime->Player().Stop(); },
                                      .Enabled = playing });
    offer("transport.profiles", "Cook Profiles...", {
        .Invoke = [this] { if (ProfilesModal) ProfilesModal->Open(); },
        .Enabled = [this] { return ProfilesModal != nullptr; } });
    OfferProfileActions();
}

void LevelWorkspace::OfferProfileActions()
{
    if (CookRuntime == nullptr)
        return;
    const std::vector<CookProfile> profiles = CookRuntime->GetSession().AvailableProfiles();
    std::vector<std::string> offered;
    for (const CookProfile& profile : profiles)
    {
        const std::string action = "transport.profile." + profile.Id;
        const std::string label = "Cook with " + profile.Name;
        if (const ActionDefinition* existing = Catalog.Find(action); existing != nullptr && existing->Label != label)
            (void)Catalog.Relabel(action, label);
        OfferAction(Catalog, ActionHandlers, { .Id = action, .Label = label, .Category = "Transport" },
                    { .Invoke = [this, id = profile.Id] { if (CookRuntime) CookRuntime->SelectProfile(id); },
                      .Checked = [this, id = profile.Id] {
                          return CookRuntime != nullptr && CookRuntime->SelectedProfileId() == id;
                      } });
        offered.push_back(action);
    }
    for (const std::string& action : OfferedProfiles)
        if (std::ranges::find(offered, action) == offered.end())
            ActionHandlers.Release(action);
    OfferedProfiles = std::move(offered);
}

bool LevelWorkspace::HoldsPointer() const
{
    return Router != nullptr && Router->PointerCaptured();
}

WorkspaceDocumentFacet LevelWorkspace::Document() const
{
    if (Files == nullptr)
        return {};
    WorkspaceDocumentFacet facet{ .Label = Files->DocumentLabel() };
    if (LevelDocument != nullptr)
    {
        std::vector<DocumentRef> changed;
        LevelDocument->AppendChangedDocuments(changed);
        facet.Changed = !changed.empty();
    }
    return facet;
}

WorkspaceViewFacet LevelWorkspace::Viewing() const
{
    const EditorViewport* active = Workspace->Layout.Active();
    return { .Projection = active != nullptr ? active->GetDisplayLabel() : "",
             .Views = static_cast<int>(TileCount(Arrangement())) };
}

WorkspaceManipulationFacet LevelWorkspace::Manipulation() const
{
    return ManipulationOf(*Workspace);
}

WorkspaceSelectionFacet LevelWorkspace::Selection()
{
    if (Verbs == nullptr || Entities == nullptr || Uv == nullptr || Components == nullptr || Partition == nullptr
        || Lighting == nullptr)
        return {};
    return SelectionOf(*Workspace, *Verbs, *Entities, *Uv, *Components, *Partition, *Lighting);
}

std::vector<WorkspaceExplorerInfo> LevelWorkspace::Explorers() const
{
    std::vector<WorkspaceExplorerInfo> explorers{ WorldExplorer::Info() };
    if (Workspace->World.IsWorld())
        explorers.push_back(PartitionOutline::Info());
    explorers.push_back(MaterialExplorerInfo());
    explorers.push_back(PrefabExplorer::Info());
    return explorers;
}

ExplorerPage LevelWorkspace::Explore(const ExplorerQuery& query)
{
    if (query.Explorer == MaterialExplorer::kId && MaterialsExplorer != nullptr)
        return MaterialsExplorer->Explore(query);
    if (query.Explorer == PrefabExplorer::kId && PrefabsExplorer != nullptr)
        return PrefabsExplorer->Explore(query);
    if (query.Explorer == WorldExplorer::kId && WorldOutline != nullptr)
        return WorldOutline->Explore(query);
    if (query.Explorer == PartitionOutline::kId && Partition != nullptr)
        return Partition->Explore(query);
    return {};
}

void LevelWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                                      std::size_t mark)
{
    if (explorer == MaterialExplorer::kId && MaterialsExplorer != nullptr)
        MaterialsExplorer->Choose(key, choice);
    else if (explorer == PrefabExplorer::kId && PrefabsExplorer != nullptr)
        PrefabsExplorer->Choose(key, choice);
    else if (explorer == WorldExplorer::kId && WorldOutline != nullptr)
        WorldOutline->Choose(key, choice, mark);
    else if (explorer == PartitionOutline::kId && Partition != nullptr)
        Partition->Choose(key, choice, mark);
}

UiInputCapture LevelWorkspace::WindowUiCapture() const
{
    const UiInputCapture legacy = UiFeature != nullptr ? UiFeature->GetInputCapture() : UiInputCapture{};
    UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr;
    return CombineUiCapture(legacy, false, ui != nullptr ? ui->Capture() : UiInputCapture{});
}

void LevelWorkspace::PublishProblems()
{
    if (LevelDocument == nullptr)
        return;
    const DocumentRef document{ .Source = LevelDocument.get(), .Key = {} };
    if (Partition != nullptr)
        Problems.Publish("world", Partition->Problems(document));
    if (Lighting != nullptr)
        Problems.Publish("lighting", Lighting->Problems(document));
}

bool LevelWorkspace::RevealProblem(const Problem& problem)
{
    return (Partition != nullptr && Partition->Reveal(problem.Location))
        || (Lighting != nullptr && Lighting->Reveal(problem.Location));
}

AssetType LevelWorkspace::HeldType() const
{
    // The tool in hand says what it consumes; without a say of its own, what
    // the Level holds is the material its face verbs apply.
    const ToolRegistry* tools = Workspace->Interaction.Tools.get();
    const ITool* tool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    if (tool != nullptr)
        for (const ResourceRole& role : tool->Resources())
            if (role.Primary)
                return role.Type;
    return AssetType::Material;
}

std::vector<WorkspaceResourceFacet> LevelWorkspace::Resources()
{
    const AssetType type = HeldType();
    const bool prefab = type == AssetType::Scene;
    SceneThumbnailCache* scenes = RenderFeature != nullptr ? RenderFeature->SceneThumbnails() : nullptr;
    const auto held = [&](const AssetRef& ref) -> HeldResource {
        if (!ref.IsValid())
            return {};
        if (prefab)
            return { .Label = std::string(SceneSourceStem(ref.Path)),
                     .Image = scenes != nullptr ? scenes->ImageName(ref.Path) : std::string{} };
        return { .Label = MaterialLeafName(ref.Path),
                 .Image = Thumbnails != nullptr ? Thumbnails->ImageName(ref.Path) : std::string{} };
    };
    WorkspaceResourceFacet facet{ .Role = prefab ? "Prefab" : "Material",
                                  .Primary = true,
                                  .Explorer = std::string(prefab ? PrefabExplorer::kId : MaterialExplorer::kId),
                                  .Active = held(Workspace->Resources.Active(type)) };
    for (const AssetRef& slot : Workspace->Resources.Stash(type))
        facet.Stash.push_back(held(slot));
    return { std::move(facet) };
}

WorkspaceOperationFacet LevelWorkspace::Operation()
{
    ToolRegistry* tools = Workspace->Interaction.Tools.get();
    ITool* tool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    if (tool == nullptr)
        return {};
    const std::span<const ITool::Variant> variants = tool->GetVariants();
    const int variant = tool->GetActiveVariant(tools->GetContext());
    std::vector<OperationChoice> choices;
    for (const std::unique_ptr<ITool>& offered : tools->GetTools())
        if (offered != nullptr)
            choices.push_back({ .Label = std::string(offered->GetDisplayName()),
                                .Icon = offered->GetIcon(),
                                .Action = "tool." + std::string(offered->GetId()),
                                .Current = offered.get() == tool });
    return {
        .Name = std::string(tool->GetDisplayName()),
        .Variant = variant >= 0 && variant < static_cast<int>(variants.size())
            ? std::string(variants[static_cast<std::size_t>(variant)].Label)
            : std::string{},
        .Icon = tool->GetIcon(),
        .Draft = tool->Draft(),
        .Hint = tool->Hint(tools->GetContext()),
        .Parameters = ToolParameters.get(),
        .Choices = std::move(choices),
    };
}

WorkspaceTransportFacet LevelWorkspace::Transport() const
{
    if (CookRuntime == nullptr)
        return {};
    const CookSession::Status status = CookRuntime->GetSession().GetStatus();
    if (CookRuntime->Player().IsPlaying())
        return { .State = TransportState::Playing };
    if (status.Active)
    {
        std::string text = status.ProfileName;
        if (!status.StepName.empty())
            text += ": " + status.StepName;
        if (status.TotalSteps > 0)
            text += " (" + std::to_string(status.CompletedSteps) + "/" + std::to_string(status.TotalSteps) + ")";
        return { .State = TransportState::Cooking, .Readout = std::move(text) };
    }
    if (!status.LastError.empty())
        return { .State = TransportState::Failed, .Readout = status.LastError };
    return { .State = TransportState::Idle };
}
