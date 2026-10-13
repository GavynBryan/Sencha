#include "LevelWorkspace.h"

#include "actions/ActionCatalog.h"
#include "notices/NoticeQueue.h"
#include "project/MaterialLibraryActions.h"
#include "project/ProjectFiles.h"

#include "viewport/EditorViewportCameraSystem.h"
#include "editmodes/ManipulatorSession.h"
#include "workspace/LevelLocation.h"
#include "workspace/BrushManipulationSink.h"
#include "input/KeymapFile.h"
#include "tools/ToolRegistry.h"
#include "input/ViewportToolDispatcher.h"
#include "input/OriginViewportStamp.h"
#include "input/SdlEventTranslation.h"
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
#include <world/identity/PersistentIdComponent.h>
#include "ui/EditorUiStyle.h"
#include "document/commands/SceneInstanceCommands.h"
#include "meshedit/FaceMaterialEdits.h"
#include "render/RenderFeatureDetach.h"
#include "documents/DocumentSourceSet.h"
#include "ui/MaterialThumbnailCache.h"
#include "render/SceneThumbnailCache.h"
#include "actions/ActionOffer.h"
#include "actions/ContextMenuModel.h"
#include "selection/commands/SelectCommand.h"
#include "resources/ResourceActions.h"
#include "authoring/ComponentPropertySource.h"
#include "workspace/ElementVerbs.h"
#include "workspace/FaceUvSource.h"
#include "workspace/MaterialExplorer.h"
#include "workspace/PrefabExplorer.h"
#include "workspace/EntityVerbs.h"
#include "workspace/LightingDiagnostics.h"
#include "document/EntityLabel.h"
#include "workspace/HierarchyOutline.h"
#include "workspace/PartitionOutline.h"
#include "workspace/PropertyHandles.h"
#include "ui/WorldMapSurface.h"
#include "viewport/CameraFraming.h"
#include "viewport/ViewArrangementSettings.h"
#include "workspace/WorldExplorer.h"
#include "scene_source/SceneSourcePaths.h"
#include "document/EntityNameComponent.h"
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
constexpr std::string_view kEditorRenderFeatureId = "editor_render";
} // namespace

ActionDefinition NewLevelAction()
{
    return { .Id = "file.new", .Label = "New Level", .Category = "File",
             .DefaultChords = { { SDLK_N, { .Ctrl = true } } } };
}

ActionDefinition NewWorldAction()
{
    return { .Id = "file.new_world", .Label = "New World", .Category = "File" };
}

LevelWorkspace::LevelWorkspace(Engine& engine,
                               SdlWindow& window,
                               ProjectDescriptor* project,
                               Game* module,
                               MaterialLibrary& materials,
                               DocumentSourceSet& documents,
                               ActionCatalog& actions, PromptQueue& prompts, ProblemList& problems,
                               NoticeQueue& notices, AssetPictures& pictures)
    : Documents(documents)
    , Prompts(prompts.MakeAsker())
    , Problems(problems)
    , Notices(notices)
    , Catalog(actions)
    , Materials(&materials)
    , Project(project)
    , Pictures(pictures)
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
    // Before the render feature that owns the studio goes.
    ScenePictures = {};
    if (Window != nullptr)
        SetRelativeMouseMode(*Window, false);
    // Its problems go with it.
    Problems.Clear("world");
    Problems.Clear("lighting");

    // An edit still open ends while what it edits is alive; then everything
    // bound to the workspace goes before the workspace does.
    if (Workspace != nullptr)
        Workspace->CancelOpenEdits();
    Lighting.reset();
    Hierarchy.reset();
    Partition.reset();
    WorldOutline.reset();
    Entities.reset();
    PrefabsExplorer.reset();
    MaterialsExplorer.reset();
    Handles.reset();
    ComponentEdits.reset();
    Components.reset();
    Uv.reset();
    Verbs.reset();
    ToolParameters.reset();

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
            // Fly-look only: hide the cursor and switch to relative mouse.
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
        { "edit.select_all",       "Select All",                  "Edit",     { SDLK_A, { .Ctrl = true } },               [this] { Workspace->SelectAll(); } },
        { "edit.repeat",           "Repeat Last",                 "Edit",     { SDLK_R, { .Ctrl = true } },               [this] { Workspace->Actions.RepeatLast(); } },
        { "file.open",             "Open...",                     "File",     { SDLK_O, { .Ctrl = true } },               [this] { AfterSettlingChanges([this] { Files->RequestOpen(); }); } },
    };
    const ActionOffers offer{ Catalog, ActionHandlers };
    for (const LevelAction& action : actions)
        offer({ .Id = std::string(action.Id), .Label = std::string(action.Label),
                .Category = std::string(action.Category), .DefaultChords = { action.Chord } },
              { .Invoke = action.Invoke });
    OfferAction(Catalog, ActionHandlers, StepBackAction(), { .Invoke = [this] { Workspace->EscapeStep(); } });
    OfferAction(Catalog, ActionHandlers, DeleteAction(), { .Invoke = [this] {
                    const EditorWorkspace::Deletion deletion = Workspace->DeleteSelection();
                    if (!deletion.WhyNot.empty())
                        Notices.Post(deletion.WhyNot, NoticeTone::Warning, NoticeClockNow());
                } });
    OfferAction(Catalog, ActionHandlers, DuplicateAction(),
                { .Invoke = [this] { Workspace->Actions.Duplicate(/*asInstance*/ false); } });
    OfferAction(Catalog, ActionHandlers, SaveAction(), { .Invoke = [this] { Files->Save(); } });
    offer({ .Id = "file.save_as", .Label = "Save As...", .Category = "File" },
          { .Invoke = [this] { Files->RequestSaveAs(); } });
    OfferAction(Catalog, ActionHandlers, NewLevelAction(),
                { .Invoke = [this] { AfterSettlingChanges([this] { Files->New(); }); } });
    OfferAction(Catalog, ActionHandlers, NewWorldAction(),
                { .Invoke = [this] { AfterSettlingChanges([this] { Files->NewWorld(); }); } });
    OfferManipulationActions(Catalog, ActionHandlers, *Workspace);
    Verbs = std::make_unique<ElementVerbs>(*Workspace, [this] { ExportSelectionGlb(); });
    Verbs->Offer(Catalog, ActionHandlers);
    Uv = std::make_unique<FaceUvSource>(*Workspace, Materials);
    Uv->Offer(Catalog, ActionHandlers);
    Components = std::make_unique<ComponentPropertySource>(Workspace->World, Workspace->Selection, *Commands,
                                                           Workspace->Affordances->Registry());
    // The sheet's title is the name and its subtitle the id.
    Components->ShowElsewhere({ ResolveComponentTypeId<EntityNameComponent>(),
                                ResolveComponentTypeId<PersistentIdComponent>() });
    ComponentEdits = Workspace->RegisterPendingEdit({ [this] { return Components->HasOpenEdit(); },
                                                      [this] { Components->CommitOpenEdit(); },
                                                      [this] { Components->CancelOpenEdit(); } });
    Handles = std::make_unique<PropertyHandles>(*Workspace, *Components);
    Entities = std::make_unique<EntityVerbs>(*Workspace, [this](const std::string& source) {
        return Files != nullptr && Files->OpenSceneSource(source);
    });
    Entities->Offer(Catalog, ActionHandlers);
    Entities->OfferZoneMoves(Catalog, ActionHandlers);
    WorldOutline = std::make_unique<WorldExplorer>(*Workspace);
    Partition = std::make_unique<PartitionOutline>(*Workspace);
    Hierarchy = std::make_unique<HierarchyOutline>(*Workspace, *WorldOutline, *Partition);
    OfferAction(Catalog, ActionHandlers,
                { .Id = "explorer.hierarchy", .Label = "Hierarchy", .Category = "Explore",
                  .DefaultChords = { { SDLK_TAB, {} } } },
                {});
    // The explorers: defined here, opened by whichever shell shows the workspace.
    OfferAction(Catalog, ActionHandlers, MaterialExplorerAction(), {});
    OfferAction(Catalog, ActionHandlers,
                { .Id = "explorer.prefabs", .Label = "Prefabs", .Category = "Explore",
                  .Icon = IconId::Box, .DefaultChords = { { SDLK_P, { .Shift = true } } } },
                {});
    OfferAction(Catalog, ActionHandlers, MaterialRescanAction(), { .Invoke = [this] {
                    if (Materials == nullptr)
                        return;
                    Materials->Rescan(Materials->Roots());
                    // A rescan may follow a save that re-pointed a material's textures.
                    Pictures.Forget();
                } });
    OfferResourceActions(Catalog, ActionHandlers, Workspace->Resources, [this] { return HeldType(); });

    ViewArrangement = std::make_unique<ViewArrangementSettings>(EnginePtr->Console().Registry());
    ViewArrangement->Offer(Catalog, ActionHandlers, Workspace->Layout);

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
            offer({ .Id = "tool." + id, .Label = std::string(tool->GetDisplayName()), .Category = "Tool",
                    .Icon = tool->GetIcon(), .DefaultChords = std::move(chords) },
                  { .Invoke = [this, id] {
                        if (ToolRegistry* live = Workspace->Interaction.Tools.get())
                            (void)live->Activate(id);
                    } });
        }
    }

    // A wheel's key is held rather than pressed, so the session owns both
    // edges of it and no handler claims the action; it is defined so the
    // keymap, hints and palette name it like any other.
    offer({ .Id = "tool.wheel", .Label = "Tool Wheel", .Category = "Tool", .DefaultChords = { { SDLK_Q, {} } } },
          {});
    offer({ .Id = "gizmo.wheel", .Label = "Transform Wheel", .Category = "Transform",
            .DefaultChords = { { SDLK_Z, {} } } },
          {});
    offer({ .Id = "context.wheel", .Label = "Context Wheel", .Category = "Edit",
            .DefaultChords = { { SDLK_SPACE, {} } } },
          {});
    OfferAction(Catalog, ActionHandlers, { .Id = std::string(kMapAction), .Label = "World Map", .Category = "View" },
                { .Invoke = [this] { Showing = Showing == LevelContent::Map ? LevelContent::Views : LevelContent::Map; },
                  .Enabled = [this] { return Workspace->World.IsWorld(); },
                  .Checked = [this] { return Showing == LevelContent::Map; } });

    // A drafted operation's Apply and Cancel, for whatever presents them. The
    // tools take Enter and Escape themselves, so these carry no chord.
    const auto activeTool = [this]() -> std::pair<ITool*, ToolRegistry*> {
        ToolRegistry* tools = Workspace->Interaction.Tools.get();
        return { tools != nullptr ? tools->GetActiveTool() : nullptr, tools };
    };
    offer({ .Id = "tool.apply", .Label = "Apply", .Category = "Tool", .Icon = IconId::Check },
          { .Invoke = [activeTool] {
                if (const auto [tool, tools] = activeTool(); tool != nullptr && tool->Draft() == ToolDraft::Ready)
                    tool->CommitPending(tools->GetContext());
            } });
    offer({ .Id = "tool.cancel", .Label = "Cancel", .Category = "Tool", .Icon = IconId::Cancel },
          { .Invoke = [activeTool] {
                if (const auto [tool, tools] = activeTool(); tool != nullptr && tool->Draft() != ToolDraft::None)
                    tool->OnCancel(tools->GetContext());
            } });
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
            return Showing == LevelContent::Views && Workspace->Layout.ResolveAt(pointer).IsValid()
                && !WindowUiCapture().Mouse;
        };
        ToolMenu = std::make_unique<ToolRegistryMenuModel>(*Workspace->Interaction.Tools);
        ToolWheel = std::make_unique<RadialMenuSession>(*ToolMenu, chordFor("tool.wheel", SDLK_Q), frame, pointerOnScene);
        GizmoMenu = std::make_unique<TransformModeMenuModel>([this] { return Workspace->Interaction.Manipulators; });
        GizmoWheel = std::make_unique<RadialMenuSession>(*GizmoMenu, chordFor("gizmo.wheel", SDLK_Z), frame, pointerOnScene);
        ContextMenu = std::make_unique<ContextMenuModel>(
            ContextMenuModel::Hooks{ .Aim = [this](Vec2d pointer) { AimAt(pointer); },
                                     .Selection = [this] { return Selection(); },
                                     .Actions = [this] { return WindowActions; },
                                     .Details = DetailsAction().Id,
                                     .Recent = [this](AssetType type) { return AssetsToHand(type); },
                                     .Browse = [](AssetType type) {
                                         return type == AssetType::Material ? MaterialExplorerAction().Id
                                                                            : std::string{};
                                     } },
            Catalog);
        // Its entries are words and its hub reads out values: a roomier wheel.
        ContextWheel = std::make_unique<RadialMenuSession>(*ContextMenu, chordFor("context.wheel", SDLK_SPACE), frame,
                                                           pointerOnScene,
                                                           RadialMenu::Proportions{ .Radius = 112.0f, .Hub = 60.0f,
                                                                                    .Button = 44.0f });
    }

    Router = std::make_unique<InputRouter>();
    // The map, while it shows, takes the pointer and Escape from everything
    // under it.
    Router->AddHandler([this](const InputEvent& e, PointerCapture&) { return OnMapInput(e); });
    // The wheels sit under the guard (a focused text field keeps its letters)
    // and above everything else: an open wheel owns the pointer and the keys,
    // which is also what keeps the other wheel closed, and each yields its key
    // to any gesture already holding the pointer.
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return ToolWheel->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return GizmoWheel->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return ContextWheel->OnInput(e, cap); });
    // A selected object's handles are taken before the tools, so a knob is
    // dragged rather than a brush drawn over it.
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Handles->OnInput(e, cap); });
    DividerDrag = std::make_unique<ViewDividerDrag>(
        [this] {
            return ViewDividerDrag::Arrangement{ .Kind = ViewArrangement->Kind(),
                                                 .Content = ContentRect,
                                                 .Divider = ViewArrangement->Divider() };
        },
        [this](Vec2d divider) { ViewArrangement->SetDivider(divider); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return DividerDrag->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Navigation->OnInput(e, cap); });
    Router->AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Workspace->Interaction.Dispatcher->OnInput(e, cap); });

}

void LevelWorkspace::BuildViewportRendering()
{
    Engine& engine = *EnginePtr;
    ConsoleService& console = engine.Console();

    EditorRenderFeature::RegisterCVars(console.Registry());

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
        FeatureRegistration{ .Id = kEditorRenderFeatureId });
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
        if (UiService* ui = EnginePtr->TryUi(); ui != nullptr && ui->IsReady())
            MapSurface = std::make_unique<WorldMapSurface>(*ui);
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
            ScenePictures = Pictures.Provide(".sscene", *cache);
        }
        PrefabsExplorer = std::make_unique<PrefabExplorer>(*Workspace);
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
    MaterialsExplorer = std::make_unique<MaterialExplorer>(*Workspace);

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

void LevelWorkspace::PresentMap()
{
    if (Showing == LevelContent::Map && !Workspace->World.IsWorld())
        Showing = LevelContent::Views;
    if (MapSurface == nullptr)
        return;
    if (Showing != LevelContent::Map || !Visible)
    {
        MapSurface->Present(nullptr, {}, {}, {});
        return;
    }
    // The plates stay clear of the breadcrumb and view instrument above and
    // the stations below.
    const float scale = EditorUi::UiScale;
    const Rect2d area = Rect2d::FromMinMax(ContentRect.Min() + Vec2d(24.0f, 48.0f) * scale,
                                           ContentRect.Max() - Vec2d(24.0f, 96.0f) * scale);
    std::vector<ZoneId> problems;
    if (Partition != nullptr && LevelDocument != nullptr)
        for (const Problem& problem : Partition->Problems(LevelDocument->Ref()))
            if (const std::optional<LevelLocation> location = ParseLevelLocation(problem.Location))
                if (const ZoneId* zone = std::get_if<ZoneId>(&*location))
                    problems.push_back(*zone);
    const EditorViewport* view = Workspace->Layout.Active();
    const std::vector<ZoneGraphEdge> edges = ZoneGraphEdges(Workspace->World);
    Map = LayOutWorldMap(Workspace->World, edges,
                         { .Area = area,
                           .Margin = 0.0f,
                           .MinimumPlate = 18.0f * scale,
                           .Camera = view != nullptr ? std::optional<Vec3d>(view->Camera.Position) : std::nullopt,
                           .Problems = problems });
    const std::string& name = Workspace->World.Manifest().Name;
    MapSurface->Present(&Map, ContentRect, name.empty() ? std::string("World") : name, MapHot);
}

InputConsumed LevelWorkspace::OnMapInput(const InputEvent& event)
{
    if (Showing != LevelContent::Map)
        return InputConsumed::No;
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
    {
        MapHot = ZoneAt(Map, move->Position);
        return InputConsumed::Yes;
    }
    if (const auto* down = std::get_if<PointerDownEvent>(&event))
    {
        if (down->Button == MouseButton::Left)
            if (const std::optional<ZoneId> zone = ZoneAt(Map, down->Position))
                Travel(*zone);
        return InputConsumed::Yes;
    }
    if (const auto* key = std::get_if<KeyDownEvent>(&event); key != nullptr && key->Key == SDLK_ESCAPE)
    {
        Showing = LevelContent::Views;
        return InputConsumed::Yes;
    }
    return std::holds_alternative<PointerUpEvent>(event) || std::holds_alternative<WheelEvent>(event)
        ? InputConsumed::Yes
        : InputConsumed::No;
}

void LevelWorkspace::Travel(ZoneId zone)
{
    WorldDocument& world = Workspace->World;
    if (zone != world.FocusZone() && !world.SetFocusZone(zone))
        return;
    for (const ZoneHeader& header : world.Manifest().Zones)
        if (header.Id == zone && header.Bounds.IsValid())
            if (EditorViewport* view = Workspace->Layout.Active())
                FrameBounds(view->Camera, header.Bounds);
    Showing = LevelContent::Views;
    MapHot.reset();
}

std::vector<ContextMenuModel::AssetChoice> LevelWorkspace::AssetsToHand(AssetType type)
{
    std::vector<ContextMenuModel::AssetChoice> choices;
    const auto add = [&](const AssetRef& ref) {
        if (ref.Path.empty() || std::ranges::any_of(choices, [&](const auto& c) { return c.Path == ref.Path; }))
            return;
        choices.push_back({ .Path = ref.Path,
                            .Label = MaterialLeafName(ref.Path),
                            .Image = type == AssetType::Material ? Pictures.PictureOf(ref.Path).Image
                                                                 : std::string{} });
    };
    add(Workspace->Resources.Active(type));
    for (const AssetRef& stashed : Workspace->Resources.Stash(type))
        add(stashed);
    return choices;
}

void LevelWorkspace::AimAt(Vec2d pointer)
{
    const EditorViewport* viewport = Workspace->Layout.Find(Workspace->Layout.ResolveAt(pointer));
    if (viewport == nullptr)
        return;
    const SelectableRef picked =
        Workspace->Picking.Pick(*viewport, pointer, Workspace->World.FocusDocument().GetScene(),
                                BrushPickRequest{ .Mode = PickModeForElementKind(Workspace->MeshEdit.GetElementKind()) });
    // Nothing there, or something already selected: the wheel is about the selection.
    const std::span<const SelectableRef> selected = Workspace->Selection.GetSelection();
    if (!picked.IsValid() || std::ranges::find(selected, picked) != selected.end())
        return;
    SelectionSnapshot only;
    only.Primary = picked;
    only.Items.push_back(picked);
    Workspace->Commands.Execute(std::make_unique<SelectCommand>(Workspace->Selection, std::move(only)));
}

void LevelWorkspace::MoveTo(const WorkspaceWindow& window)
{
    WindowActions = window.Actions;
    if (window.Window != nullptr && window.Window != Window)
    {
        if (Window != nullptr)
            SetRelativeMouseMode(*Window, false);
        Window = window.Window;
        if (Files != nullptr)
            Files->SetWindow(*Window);
    }
    if (ViewSurface != nullptr)
        ViewSurface->MoveTo(window.Content);
    if (MapSurface != nullptr)
        MapSurface->MoveTo(window.Content);
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
        ViewSurface->Arrange(Visible ? ContentRect : Rect2d{}, ViewArrangement->Kind(), ViewArrangement->Divider(),
                             EditorUi::UiScale);
    if (Visible)
        Handles->Publish();
    PresentMap();
    // Only one wheel is ever open: each yields its key to the other.
    if (WheelScreen != nullptr)
    {
        if (ToolWheel->GetPhase() == RadialMenuPhase::Open)
            WheelScreen->Present(ToolWheel.get(), ToolMenu.get());
        else if (GizmoWheel->GetPhase() == RadialMenuPhase::Open)
            WheelScreen->Present(GizmoWheel.get(), GizmoMenu.get());
        else if (ContextWheel->GetPhase() == RadialMenuPhase::Open)
            WheelScreen->Present(ContextWheel.get(), ContextMenu.get());
        else
            WheelScreen->Present(nullptr, nullptr);
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

void LevelWorkspace::DefineTransportActions()
{
    // The transport as actions: the rail shows the keys, the console and the
    // palette reach the same handlers. A cook reads the live documents (and
    // force-saves the world first), so open previews settle before it starts or
    // they would cook half-staged.
    const ActionOffers offer{ Catalog, ActionHandlers, "Transport" };
    const auto cooking = [this] { return CookRuntime != nullptr && CookRuntime->IsActive(); };
    const auto playing = [this] { return CookRuntime != nullptr && CookRuntime->Player().IsPlaying(); };
    offer({ .Id = "transport.cook", .Label = "Cook" }, {
        .Invoke = [this] { Workspace->ResolvePendingEdits(); if (CookRuntime) CookRuntime->Start(); },
        .Enabled = [this, cooking] { return CookRuntime != nullptr && !cooking(); } });
    offer({ .Id = "transport.rebuild", .Label = "Cook From Scratch" }, {
        .Invoke = [this] { Workspace->ResolvePendingEdits(); if (CookRuntime) CookRuntime->Start(/*rebuild*/ true); },
        .Enabled = [this, cooking] { return CookRuntime != nullptr && !cooking(); } });
    offer({ .Id = "transport.cancel", .Label = "Cancel Cook" }, {
        .Invoke = [this] { if (CookRuntime) CookRuntime->Cancel(); },
        .Enabled = cooking });
    offer({ .Id = "transport.play", .Label = "Play" }, {
        .Invoke = [this] { if (CookRuntime) CookRuntime->Player().Play(CookRuntime->Player().LastCookedMap()); },
        .Enabled = [this, playing] { return CookRuntime != nullptr && !playing(); } });
    offer({ .Id = "transport.stop", .Label = "Stop" },
          { .Invoke = [this] { if (CookRuntime) CookRuntime->Player().Stop(); }, .Enabled = playing });
    offer({ .Id = "transport.profiles", .Label = "Cook Profiles..." }, {
        .Invoke = [this] { if (ProfilesModal) ProfilesModal->Open(); },
        .Enabled = [this] { return ProfilesModal != nullptr; } });
    OfferProfileActions();
}

void LevelWorkspace::OfferProfileActions()
{
    if (CookRuntime == nullptr)
        return;
    const std::vector<CookProfile> profiles = CookRuntime->GetSession().AvailableProfiles();
    std::vector<ActionDefinition> members;
    for (const CookProfile& profile : profiles)
        members.push_back(
            { .Id = "transport.profile." + profile.Id, .Label = "Cook with " + profile.Name, .Category = "Transport" });
    ProfileActions.Offer(Catalog, ActionHandlers, std::move(members), [this, &profiles](std::size_t index) {
        const std::string& id = profiles[index].Id;
        return ActionBinding{
            .Invoke = [this, id] { if (CookRuntime) CookRuntime->SelectProfile(id); },
            .Checked = [this, id] { return CookRuntime != nullptr && CookRuntime->SelectedProfileId() == id; } };
    });
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
             .Views = static_cast<int>(TileCount(ViewArrangement->Kind())) };
}

WorkspaceLocationFacet LevelWorkspace::Location() const
{
    WorkspaceLocationFacet facet{ .Parts = {},
                                  .Hierarchy = std::string(HierarchyOutline::kId),
                                  .MapAction = Workspace->World.IsWorld() ? std::string(kMapAction) : std::string{} };
    const WorldDocument& world = Workspace->World;
    if (world.IsWorld())
    {
        const WorldPartitionManifest& manifest = world.Manifest();
        facet.Parts.push_back({ .Label = manifest.Name.empty() ? std::string("World") : manifest.Name, .Key = "world" });
        // The zone the camera is in, or, outside every zone, the one being edited.
        const ZoneHeader* here = nullptr;
        if (const EditorViewport* view = Workspace->Layout.Active())
            here = InnermostZoneAt(manifest, view->Camera.Position);
        if (here == nullptr)
            for (const ZoneHeader& zone : manifest.Zones)
                if (zone.Id == world.FocusZone())
                    here = &zone;
        if (here != nullptr)
        {
            facet.Parts.push_back({ .Label = GraphDisplayName(manifest, here->Graph),
                                    .Key = LevelLocationKey(here->Graph) });
            facet.Parts.push_back({ .Label = ZoneDisplayName(manifest, here->Id),
                                    .Key = LevelLocationKey(here->Id) });
        }
    }
    else if (Files != nullptr)
    {
        facet.Parts.push_back({ .Label = Files->DocumentLabel(), .Key = {} });
    }
    const SelectableRef primary = Workspace->Selection.GetPrimarySelection();
    const EditorDocument& document = world.FocusDocument();
    const auto objects = std::ranges::count_if(Workspace->Selection.GetSelection(), &SelectableRef::IsEntity);
    if (primary.IsEntity() && document.GetScene().HasEntity(primary.Entity))
        facet.Parts.push_back({ .Label = objects > 1 ? std::format("{} objects", objects)
                                                     : EntityLabel(document, primary.Entity),
                                .Key = LevelLocationKey(document.GetScene().PersistentIdOf(primary.Entity)),
                                .Selection = true });
    return facet;
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
    WorkspaceSelectionFacet selection = SelectionOf(*Workspace, *Verbs, *Entities, *Uv, *Components, *Partition, *Lighting);
    // The view stays clear: what is selected is looked at through the context
    // wheel, and in full when asked for.
    selection.OnRequest = true;
    return selection;
}

std::vector<WorkspaceExplorerInfo> LevelWorkspace::Explorers() const
{
    std::vector<WorkspaceExplorerInfo> explorers{ HierarchyOutline::Info() };
    if (MaterialsExplorer != nullptr)
        explorers.push_back(MaterialsExplorer->Info());
    if (PrefabsExplorer != nullptr)
        explorers.push_back(PrefabsExplorer->Info());
    return explorers;
}

ExplorerPage LevelWorkspace::Explore(const ExplorerQuery& query)
{
    if (query.Explorer == HierarchyOutline::kId && Hierarchy != nullptr)
        return Hierarchy->Explore(query);
    return {};
}

void LevelWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                                      std::size_t mark)
{
    if (explorer == MaterialExplorer::kId && MaterialsExplorer != nullptr)
        MaterialsExplorer->Choose(key, choice);
    else if (explorer == PrefabExplorer::kId && PrefabsExplorer != nullptr)
        PrefabsExplorer->Choose(key, choice);
    else if (explorer == HierarchyOutline::kId && Hierarchy != nullptr)
        Hierarchy->Choose(key, choice, mark);
}

UiInputCapture LevelWorkspace::WindowUiCapture() const
{
    UiService* ui = EnginePtr != nullptr ? EnginePtr->TryUi() : nullptr;
    return ui != nullptr && Window != nullptr ? ui->Capture(Window->GetId()) : UiInputCapture{};
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

bool LevelWorkspace::OpenAsset(const ProjectFile& file)
{
    if (Files == nullptr)
        return false;
    if (file.Extension == ".sworld")
    {
        Files->OpenFile(file.FilePath.string());
        return true;
    }
    return Files->OpenSceneSource(file.AssetPath);
}

bool LevelWorkspace::DropAsset(const ProjectFile& file, Vec2d point)
{
    const EditorViewport* viewport = Workspace->Layout.Find(Workspace->Layout.ResolveAt(point));
    if (viewport == nullptr || Showing != LevelContent::Views)
        return false;
    EditorDocument& document = Workspace->ActiveDocument();
    const AssetRef ref{ file.Extension == ".smat" ? AssetType::Material : AssetType::Scene, file.AssetPath,
                        AssetIdOf(file) };
    if (file.Extension == ".smat")
    {
        // On the face under it -- on every selected face, when that is one of them.
        const SelectableRef face = Workspace->Picking.Pick(*viewport, point, document.GetScene(),
                                                           BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
        if (!face.IsFace() || Workspace->Interaction.Sink == nullptr)
            return false;
        const std::span<const SelectableRef> selected = Workspace->Selection.GetSelection();
        if (std::ranges::find(selected, face) != selected.end())
            ApplyMaterialToSelectedFaces(*Workspace->Interaction.Sink, Workspace->Selection, Workspace->Commands, ref);
        else
            ApplyMaterialToFaces(*Workspace->Interaction.Sink, { &face, 1 }, Workspace->Commands, ref);
        Workspace->Resources.SetActive(ref);
        return true;
    }
    if (file.Extension != ".sscene")
        return false;
    // A level is not placed inside itself.
    if (document.SourceAssetPath() == file.AssetPath)
        return false;
    // Where the Entity tool would put it: on the surface under it, else on the grid.
    std::optional<Vec3d> at;
    if (const std::optional<SurfaceHit> hit = Workspace->Picking.PickSurface(*viewport, point, document.GetScene()))
        at = hit->Point;
    else
        at = Workspace->Picking.ProjectPointToGrid(*viewport, point, Workspace->Grid);
    if (!at.has_value())
        return false;
    Transform3f placement = Transform3f::Identity();
    placement.Position = *at;
    Workspace->Commands.Execute(
        std::make_unique<PlaceSceneInstanceCommand>(file.AssetPath, placement, document, Workspace->Selection));
    Workspace->Resources.SetActive(ref);
    return true;
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
    const auto held = [&](const AssetRef& ref) -> HeldResource {
        if (!ref.IsValid())
            return {};
        return { .Label = prefab ? std::string(SceneSourceStem(ref.Path)) : MaterialLeafName(ref.Path),
                 .Image = Pictures.PictureOf(ref.Path).Image };
    };
    WorkspaceResourceFacet facet{ .Role = prefab ? "Prefab" : "Material",
                                  .Primary = true,
                                  .Explorer = std::string(prefab ? PrefabExplorer::kId : MaterialExplorer::kId),
                                  .Active = held(Workspace->Resources.Active(type)) };
    const std::span<const AssetRef> stash = Workspace->Resources.Stash(type);
    for (std::size_t slot = 0; slot < stash.size(); ++slot)
    {
        HeldResource stashed = held(stash[slot]);
        stashed.Action = UseSlotAction(slot);
        facet.Stash.push_back(std::move(stashed));
    }
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
