#include "LevelWorkspace.h"

#include "actions/ActionCatalog.h"
#include "notices/NoticeQueue.h"
#include "project/MaterialLibraryActions.h"
#include "project/ProjectFiles.h"

#include "viewport/EditorViewportCameraSystem.h"
#include "workspace/HeldResources.h"
#include "workspace/LevelBreadcrumb.h"
#include "workspace/BrushManipulationSink.h"
#include "tools/ToolRegistry.h"
#include "input/ViewportToolDispatcher.h"
#include "input/OriginViewportStamp.h"
#include "input/SdlEventTranslation.h"
#include "document/DocumentFileActions.h"
#include "document/DocumentSerialization.h"
#include "CookTransport.h"
#include "LightingReportFeed.h"
#include "SelectionGlbExport.h"
#include "ModuleComponents.h"
#include "EditorCookRuntime.h"
#include "project/MaterialLibrary.h"

#include "render/EditorRenderFeature.h"
#include "ui/CookProfilesModal.h"
#include <ui/UiService.h>
#include <world/identity/PersistentIdComponent.h>
#include "ui/EditorUiStyle.h"
#include "meshedit/FaceMaterialEdits.h"
#include "render/RenderFeatureDetach.h"
#include "documents/DocumentSourceSet.h"
#include "render/SceneThumbnailCache.h"
#include "actions/ActionOffer.h"
#include "actions/ContextMenuModel.h"
#include "selection/commands/SelectCommand.h"
#include "resources/ResourceActions.h"
#include "authoring/ComponentPropertySource.h"
#include "authoring/PlaceIcons.h"
#include "workspace/ElementVerbs.h"
#include "workspace/FaceUvSource.h"
#include "workspace/MaterialExplorer.h"
#include "workspace/PrefabExplorer.h"
#include "workspace/EntityVerbs.h"
#include "workspace/LightingDiagnostics.h"
#include "workspace/HierarchyOutline.h"
#include "workspace/PartitionOutline.h"
#include "workspace/PropertyHandles.h"
#include "workspace/WorldMapMode.h"
#include "ui/WorldMapSurface.h"
#include "viewport/ViewArrangementSettings.h"
#include "workspace/WorldExplorer.h"
#include "document/EntityNameComponent.h"
#include "workspace/SelectionSummary.h"
#include "workspace/ManipulationActions.h"
#include "tools/ToolActions.h"
#include "tools/ToolParameterSource.h"
#include "ui/RadialMenuScreen.h"
#include "ui/LevelViewSurface.h"
#include "workspaces/WorkspaceWindow.h"

#include <SDL3/SDL.h>


#include <app/Engine.h>
#include <core/console/ConsoleService.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>


namespace
{
constexpr std::string_view kEditorRenderFeatureId = "editor_render";
// Commands capture this workspace and go with it; its cvars are archived
// settings registered under "editor" and outlive it.
constexpr std::string_view kCommandOwner = "editor.level";
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
    Module = std::make_unique<ModuleComponents>(module);
    Problems.Publish("components", UnknownPlaceIcons(EditorSceneSerializers()));

    BuildDocument();
    // After the document: an authored workflow presents editor state, and the
    // inspector's is the selection and the command stack the document owns.
    BuildAuthoredWorkflows();
    BuildPlayLoop();
    BuildFileActions();
    OfferDocumentActions();
    BuildSelectionSources();
    BuildViewControls();
    BuildWheels();
    BuildRouter();
    BuildViewportRendering();
    BuildUi();
    CookControls = std::make_unique<CookTransport>(*CookRuntime, ProfilesModal.get(),
                                                   [this] { Workspace->ResolvePendingEdits(); });
    CookControls->Offer(Catalog, ActionHandlers);
}

LevelWorkspace::~LevelWorkspace()
{
    EnginePtr->Console().Registry().UnregisterOwner(kCommandOwner);
    // Before the render feature that owns the studio goes.
    ScenePictures = {};
    if (Window != nullptr)
        SetRelativeMouseMode(*Window, false);
    // Its problems go with it.
    Problems.Clear("world");
    Problems.Clear("lighting");
    Problems.Clear("components");

    // An edit still open ends while what it edits is alive; then everything
    // bound to the workspace goes before the workspace does.
    if (Workspace != nullptr)
        Workspace->CancelOpenEdits();
    Lighting.reset();
    MapMode.reset();
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
    CookControls.reset();
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
    Module.reset();
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
    CookRuntime->RegisterConsoleCommands(engine.Console().Registry(), kCommandOwner);
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
    Files->RegisterCommands(engine.Console().Registry(), kCommandOwner);
}

void LevelWorkspace::OfferDocumentActions()
{
    // Escape reaches its handler only when no drag is in flight (the viewport
    // dispatcher consumes it to cancel one), so it climbs the editing context
    // one level per press.
    OfferAction(Catalog, ActionHandlers, StepBackAction(), { .Invoke = [this] { Workspace->EscapeStep(); } });
    OfferAction(Catalog, ActionHandlers, DeleteAction(), { .Invoke = [this] {
                    const EditorWorkspace::Deletion deletion = Workspace->DeleteSelection();
                    if (!deletion.WhyNot.empty())
                        Notices.Post(deletion.WhyNot, NoticeTone::Warning, NoticeClockNow());
                } });
    OfferAction(Catalog, ActionHandlers, DuplicateAction(),
                { .Invoke = [this] { Workspace->Actions.Duplicate(/*asInstance*/ false); } });
    const ActionOffers edit{ Catalog, ActionHandlers, "Edit" };
    edit({ .Id = "edit.select_all", .Label = "Select All", .DefaultChords = { { SDLK_A, { .Ctrl = true } } } },
         { .Invoke = [this] { Workspace->SelectAll(); } });
    edit({ .Id = "edit.repeat", .Label = "Repeat Last", .DefaultChords = { { SDLK_R, { .Ctrl = true } } } },
         { .Invoke = [this] { Workspace->Actions.RepeatLast(); } });

    OfferAction(Catalog, ActionHandlers, SaveAction(), { .Invoke = [this] { Files->Save(); } });
    OfferAction(Catalog, ActionHandlers, NewLevelAction(),
                { .Invoke = [this] { AfterSettlingChanges([this] { Files->New(); }); } });
    OfferAction(Catalog, ActionHandlers, NewWorldAction(),
                { .Invoke = [this] { AfterSettlingChanges([this] { Files->NewWorld(); }); } });
    const ActionOffers file{ Catalog, ActionHandlers, "File" };
    file({ .Id = "file.open", .Label = "Open...", .DefaultChords = { { SDLK_O, { .Ctrl = true } } } },
         { .Invoke = [this] { AfterSettlingChanges([this] { Files->RequestOpen(); }); } });
    file({ .Id = "file.save_as", .Label = "Save As..." }, { .Invoke = [this] { Files->RequestSaveAs(); } });
}

void LevelWorkspace::BuildSelectionSources()
{
    Verbs = std::make_unique<ElementVerbs>(*Workspace, [this] {
        if (Window != nullptr)
            ExportSelectionGlb(*Window, *Workspace);
    });
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
    ExplorerRows.Offer(Catalog, ActionHandlers, Hierarchy->Row());
    OfferAction(Catalog, ActionHandlers, MaterialRescanAction(), { .Invoke = [this] {
                    if (Materials == nullptr)
                        return;
                    Materials->Rescan(Materials->Roots());
                    // A rescan may follow a save that re-pointed a material's textures.
                    Pictures.Forget();
                } });
    OfferResourceActions(Catalog, ActionHandlers, Workspace->Resources, [this] { return HeldType(*Workspace); });
}

void LevelWorkspace::BuildViewControls()
{
    Navigation = std::make_unique<ViewportNavigation>(
        Workspace->Layout,
        [this](bool enabled)
        {
            // Fly-look only: hide the cursor and switch to relative mouse.
            if (Window != nullptr)
                SetRelativeMouseMode(*Window, enabled);
        });

    OfferManipulationActions(Catalog, ActionHandlers, *Workspace);
    ViewArrangement = std::make_unique<ViewArrangementSettings>(EnginePtr->Console().Registry());
    ViewArrangement->Offer(Catalog, ActionHandlers, Workspace->Layout);

    OfferToolActions(Catalog, ActionHandlers, [this] { return Workspace->Interaction.Tools.get(); });

    MapMode = std::make_unique<WorldMapMode>(*Workspace, *Partition, LevelDocument->Ref());
    MapMode->Offer(Catalog, ActionHandlers);

    ToolParameters = std::make_unique<ToolParameterSource>([this] { return Workspace->Interaction.Tools.get(); });
}

void LevelWorkspace::BuildWheels()
{
    // A wheel's key is held rather than pressed: its session owns both edges
    // and no handler claims the action, which is defined so the keymap, hints
    // and palette name it. The wheels place themselves against the same window
    // and open over the same scene test.
    const ActionOffers offer{ Catalog, ActionHandlers };
    offer({ .Id = "gizmo.wheel", .Label = "Transform Wheel", .Category = "Transform",
            .DefaultChords = { { SDLK_Z, {} } } },
          {});
    offer({ .Id = "context.wheel", .Label = "Context Wheel", .Category = "Edit",
            .DefaultChords = { { SDLK_SPACE, {} } } },
          {});
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
            return MapMode->Content() == LevelContent::Views && Workspace->Layout.ResolveAt(pointer).IsValid()
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
                                     .Recent = [this](AssetType type) { return AssetsToHand(*Workspace, type, Pictures); },
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
}

void LevelWorkspace::BuildRouter()
{
    Router = std::make_unique<InputRouter>();
    // The map, while it shows, takes the pointer and Escape from everything
    // under it.
    Router->AddHandler([this](const InputEvent& e, PointerCapture&) { return MapMode->OnInput(e); });
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
        Workspace->Interaction.Placement,
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
        MaterialsExplorer = std::make_unique<MaterialExplorer>(*Workspace);
        ExplorerRows.Offer(Catalog, ActionHandlers, MaterialsExplorer->Row());
        Workspace->World.OnSourcesSaved = [this](std::span<const std::string> saved) {
            if (RenderFeature == nullptr)
                return;
            if (SceneThumbnailCache* cache = RenderFeature->SceneThumbnails())
                cache->Invalidate(saved);
            if (PlacementGhost* ghost = RenderFeature->PlacementGhosts())
                ghost->Invalidate(saved);
        };
        PrefabsExplorer = std::make_unique<PrefabExplorer>(*Workspace);
        ExplorerRows.Offer(Catalog, ActionHandlers, PrefabsExplorer->Row());
    }
    Lighting = std::make_unique<LightingDiagnostics>(*Workspace, *Commands,
                                                     LightingReportsFrom(RenderFeature, *CookRuntime));
    Lighting->Offer(Catalog, ActionHandlers);

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
        if (event.has_value() && Route(std::move(*event)) == InputConsumed::Yes)
            ctx.Handled = true;
    }
}

InputConsumed LevelWorkspace::Route(InputEvent event)
{
    if (Router == nullptr)
        return InputConsumed::No;
    StampOriginViewport(*Router, Workspace->Layout, event);
    return Router->Route(event);
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
    if (CookControls != nullptr)
        CookControls->Update(Catalog, ActionHandlers);

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
    const WorldMap* map = MapMode->LayOut(ContentRect, Visible);
    if (MapSurface != nullptr)
        MapSurface->Present(map, ContentRect, MapMode->Title(), MapMode->Hot());
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
    return LevelBreadcrumb(*Workspace, Files != nullptr ? Files->DocumentLabel() : std::string{});
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
    if (viewport == nullptr || MapMode->Content() != LevelContent::Views)
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
    if (!TakesCarry(file))
        return false;
    // Let go before the views took the carry: the same carry, handed over and
    // released where it was let go.
    TakeCarry(file, point);
    return Route(PointerUpEvent{ .Position = point, .Button = MouseButton::Left, .Modifiers = {} })
        == InputConsumed::Yes;
}

bool LevelWorkspace::TakesCarry(const ProjectFile& file) const
{
    // Not over a gesture that already holds the pointer, such as an open wheel.
    return MapMode->Content() == LevelContent::Views && Workspace->Interaction.Tools != nullptr && Router != nullptr
        && !Router->PointerCaptured() && file.Extension == ".sscene";
}

void LevelWorkspace::TakeCarry(const ProjectFile& file, Vec2d point)
{
    // A prefab carried in arms the Entity tool with it, as picking it does.
    Workspace->Resources.SetActive(AssetRef{ AssetType::Scene, file.AssetPath, AssetIdOf(file) });
    (void)Workspace->Interaction.Tools->Activate("place");
    (void)Route(PointerHandedEvent{ .Position = point, .Modifiers = {} });
}

bool LevelWorkspace::RevealProblem(const Problem& problem)
{
    return (Partition != nullptr && Partition->Reveal(problem.Location))
        || (Lighting != nullptr && Lighting->Reveal(problem.Location));
}

std::vector<WorkspaceResourceFacet> LevelWorkspace::Resources()
{
    return { HeldResourceFacet(*Workspace, Pictures) };
}

WorkspaceOperationFacet LevelWorkspace::Operation()
{
    return ToolOperation(Workspace->Interaction.Tools.get(), ToolParameters.get());
}

WorkspaceTransportFacet LevelWorkspace::Transport() const
{
    return CookControls != nullptr ? CookControls->Facet() : WorkspaceTransportFacet{};
}
