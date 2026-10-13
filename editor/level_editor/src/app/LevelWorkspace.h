#pragma once

#include "thumbnails/AssetPictures.h"

#include "actions/ContextMenuModel.h"
#include "document/WorldMap.h"

#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <ecs/ComponentTypeId.h>
#include <input/UiInputCapture.h>
#include <platform/WindowTypes.h>
#include <ui/UiSurface.h>

#include "commands/CommandStack.h"
#include "input/InputRouter.h"
#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "actions/ActionFamily.h"
#include "input/ViewportNavigation.h"
#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"
#include "tools/ToolRegistryMenuModel.h"
#include "editmodes/TransformModeMenuModel.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/LevelDocumentSource.h"
#include "prompts/DocumentPrompts.h"
#include "project/MaterialLibrary.h"
#include "viewport/ViewArrangementSettings.h"
#include "viewport/ViewDividerDrag.h"
#include "viewport/ViewportArrangement.h"
#include "project/Project.h"
#include "workspaces/IWorkspace.h"

#include <memory>
#include <optional>
#include <vector>

class MaterialThumbnailCache;
class LevelViewSurface;
class WorldMapSurface;
class ActionContextStack;
class RadialMenuScreen;
class ToolParameterSource;
class EditorRenderFeature;
class EditorViewportCameraSystem;
class DocumentSourceSet;
class Engine;
class Game;
class SdlWindow;
class PieDriver;
class CookSession;
class CookProfilesModal;
class ComponentPropertySource;
class ElementVerbs;
class MaterialExplorer;
class PrefabExplorer;
class NoticeQueue;
class ProblemList;
class LightingDiagnostics;
class HierarchyOutline;
class PartitionOutline;
class PropertyHandles;
class EntityVerbs;
class WorldExplorer;
class FaceUvSource;
class EditorCookRuntime;
class DocumentFileActions;

// Ctrl+N: a new level; and a new world of zones.
[[nodiscard]] ActionDefinition NewLevelAction();
[[nodiscard]] ActionDefinition NewWorldAction();

// The level editor as a workspace. The constructor is the bring-up sequence;
// member order is teardown order, and the destructor spells out the
// load-bearing part of it.
class ActionCatalog;

class LevelWorkspace final : public IWorkspace
{
public:
    // The project, the game module and the materials belong to the
    // application and outlive this.
    LevelWorkspace(Engine& engine,
                   SdlWindow& window,
                   ProjectDescriptor* project,
                   Game* module,
                   MaterialLibrary& materials,
                   DocumentSourceSet& documents,
                   ActionCatalog& actions,
                   PromptQueue& prompts,
                   ProblemList& problems,
                   NoticeQueue& notices,
                   AssetPictures& pictures);
    ~LevelWorkspace() override;

    LevelWorkspace(const LevelWorkspace&) = delete;
    LevelWorkspace& operator=(const LevelWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    // Routes one platform event through the input router chain.
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    bool UndoStagedEdit() override;
    [[nodiscard]] bool HasStagedEdit() const override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    [[nodiscard]] WorkspaceOperationFacet Operation() override;
    [[nodiscard]] WorkspaceManipulationFacet Manipulation() const override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] WorkspaceLocationFacet Location() const override;
    // Selects what is under `pointer` for the context wheel, unless it already is.
    void AimAt(Vec2d pointer);
    // The world map in place of the views: laid out from the world each frame
    // it shows, hit-tested by the pointer, and left by going somewhere or Escape.
    void PresentMap();
    InputConsumed OnMapInput(const InputEvent& event);
    // Edits `zone` with the camera on it.
    void Travel(ZoneId zone);
    // The assets of a kind to hand: the one in use, then the ones put by.
    [[nodiscard]] std::vector<ContextMenuModel::AssetChoice> AssetsToHand(AssetType type);
    [[nodiscard]] std::vector<WorkspaceResourceFacet> Resources() override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;
    // The kind of resource the hands hold now: what the tool in hand consumes.
    [[nodiscard]] AssetType HeldType() const;
    bool RevealProblem(const Problem& problem) override;
    bool OpenAsset(const ProjectFile& file) override;
    bool DropAsset(const ProjectFile& file, Vec2d point) override;
    // The world's validation, republished only when it changed.
    void PublishProblems();
    // What the window's UI wants of the pointer and keys now.
    [[nodiscard]] UiInputCapture WindowUiCapture() const;
    [[nodiscard]] WorkspaceTransportFacet Transport() const override;

private:
    void DefineTransportActions();
    void OfferProfileActions();
    // Constructor phases, in call order. Each builds one cohesive slice of the
    // editor against the engine (*EnginePtr) and primary window (*Window), so the
    // constructor reads as the bring-up sequence.
    void BuildDocument();
    void BuildPlayLoop();
    void BuildFileActions();
    void BuildInput();
    void BuildViewportRendering();
    void BuildUi();

    void ProcessFrame();
    // Runs `proceed` once the open level's changes are saved or discarded, as
    // the author chooses; at once when it has none.
    void AfterSettlingChanges(std::function<void()> proceed);
    // Saves every changed document before a cook or a play session reads the
    // disk; false, and the save's follow-up says why, when one could not be saved.
    [[nodiscard]] bool SaveBeforeLaunch(std::string& error);

    // The game module's components into the editor's serializer registry, and
    // its vocabulary into every document's World, before the document exists.
    void RegisterModuleComponents(Game* module);
    // Retracted while the module is still mapped: it built the serializers.
    void RetractModuleComponents();

    void BuildAuthoredWorkflows();

    // Writes the selection's geometry as binary glTF, behind a save dialog.
    void ExportSelectionGlb();

    // Where the window last said the views show.
    Rect2d ContentRect{};
    // Held for the Active Material panel's Browse jump (Reveal()).
    // Owned by the engine renderer; the views are drawn from its targets.
    EditorRenderFeature* RenderFeature = nullptr;
    // The views on the window's content surface; gone before RenderFeature.
    std::unique_ptr<LevelViewSurface> ViewSurface;
    std::unique_ptr<WorldMapSurface> MapSurface;
    // What fills the content: the views, or the world map over them.
    enum class LevelContent : std::uint8_t
    {
        Views,
        Map,
    };
    static constexpr std::string_view kMapAction = "view.map";
    LevelContent Showing = LevelContent::Views;
    WorldMap Map;
    std::optional<ZoneId> MapHot;
    // Whichever wheel is open, over everything in the window.
    std::unique_ptr<RadialMenuScreen> WheelScreen;
    // The window this workspace is placed in; null until it is.
    std::unique_ptr<EditorViewportCameraSystem> CameraSystem;
    Engine* EnginePtr = nullptr;
    // The window the workspace is placed in: its pointer, layout and dialogs.
    SdlWindow* Window = nullptr;
    // Where authored surfaces live, whichever window the workspace is in.
    SdlWindow* PrimaryWindow = nullptr;
    bool Visible = false;
    WindowExtent LayoutExtent{};

    // The engine's asset stack, which the application mounted the project into.
    RuntimeAssets* Assets = nullptr;

    DocumentSourceSet& Documents;
    std::unique_ptr<CommandStack> Commands;
    std::unique_ptr<EditorWorkspace> Workspace;
    // The open level in the application's journal; destroyed before the
    // workspace whose world it reports on.
    std::unique_ptr<LevelDocumentSource> LevelDocument;
    // After the document its questions act on, so they are withdrawn first.
    PromptQueue::Asker Prompts;
    // Where the world's validation and the lighting's troubles are reported.
    ProblemList& Problems;
    NoticeQueue& Notices;
    // The held-key radial menus: the tools over the workspace's registry, the
    // gizmo modes over the manipulator session. One mechanism, two models.
    std::unique_ptr<ToolRegistryMenuModel> ToolMenu;
    std::unique_ptr<RadialMenuSession> ToolWheel;
    std::unique_ptr<TransformModeMenuModel> GizmoMenu;
    std::unique_ptr<RadialMenuSession> GizmoWheel;
    std::unique_ptr<ContextMenuModel> ContextMenu;
    std::unique_ptr<RadialMenuSession> ContextWheel;
    // The window's actions, which the context wheel's entries resolve through.
    const ActionContextStack* WindowActions = nullptr;
    std::unique_ptr<InputRouter> Router;
    std::unique_ptr<ViewportNavigation> Navigation;
    std::unique_ptr<ViewArrangementSettings> ViewArrangement;
    std::unique_ptr<ViewDividerDrag> DividerDrag;
    std::unique_ptr<ElementVerbs> Verbs;
    std::unique_ptr<FaceUvSource> Uv;
    std::unique_ptr<ComponentPropertySource> Components;
    // Keeps the workspace ending an edit the source has open.
    std::shared_ptr<const void> ComponentEdits;
    std::unique_ptr<PropertyHandles> Handles;
    std::unique_ptr<MaterialExplorer> MaterialsExplorer;
    std::unique_ptr<PrefabExplorer> PrefabsExplorer;
    std::unique_ptr<EntityVerbs> Entities;
    std::unique_ptr<WorldExplorer> WorldOutline;
    std::unique_ptr<PartitionOutline> Partition;
    std::unique_ptr<HierarchyOutline> Hierarchy;
    std::unique_ptr<LightingDiagnostics> Lighting;
    ActionCatalog& Catalog;
    ActionContext ActionHandlers;
    // The tool in hand's settings, as the operation facet hands them out.
    std::unique_ptr<ToolParameterSource> ToolParameters;
    MaterialLibrary* Materials = nullptr;
    // Component identities the module's registration added serializers for,
    // so retraction removes exactly those.
    std::vector<ComponentTypeId> GameModuleSerializerTypes;

    ProjectDescriptor* Project = nullptr;
    // Pictures of content files, shared with every workspace.
    AssetPictures& Pictures;
    // Its studio pictures scenes for everyone while it is open.
    AssetPictures::Registration ScenePictures;

    // Declared last so they are torn down before the state they reference.
    // Cooking, the player it feeds, and the serials that hand one to the other.
    std::unique_ptr<EditorCookRuntime>  CookRuntime;
    std::unique_ptr<CookProfilesModal>  ProfilesModal;
    ActionFamily ProfileActions;
    std::uint64_t OfferedProfilesRevision = 0;
    std::unique_ptr<DocumentFileActions> Files;
};
