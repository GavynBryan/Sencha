#pragma once

#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <ecs/ComponentTypeId.h>
#include <platform/WindowTypes.h>
#include <ui/UiSurface.h>

#include "commands/CommandStack.h"
#include "input/InputRouter.h"
#include "actions/ActionContext.h"
#include "input/ViewportNavigation.h"
#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"
#include "tools/ToolRegistryMenuModel.h"
#include "editmodes/TransformModeMenuModel.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/LevelDocumentSource.h"
#include "prompts/DocumentPrompts.h"
#include "project/MaterialLibrary.h"
#include "viewport/ViewportArrangement.h"
#include "project/Project.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <memory>
#include <optional>
#include <vector>

class EditorUiFeature;
class MaterialPickerPanel;
class MaterialThumbnailCache;
class LevelViewSurface;
class RadialMenuScreen;
class ToolParameterSource;
class ViewsPanel;
class EditorRenderFeature;
class EditorViewportCameraSystem;
class DocumentSourceSet;
class Engine;
class Game;
class SdlWindow;
class PieDriver;
class CookSession;
class CookProfilesModal;
class InspectorSurface;
class CookProfilesPanel;
class EditorCookRuntime;
class DocumentFileActions;

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
                   PromptQueue& prompts);
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
    [[nodiscard]] bool HoldsPointer() const override;
    void MoveTo(const WorkspaceWindow& window) override;
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    [[nodiscard]] WorkspaceOperationFacet Operation() override;
    [[nodiscard]] WorkspaceManipulationFacet Manipulation() const override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] std::vector<WorkspaceResourceFacet> Resources() override;
    [[nodiscard]] WorkspaceTransportFacet Transport() const override;
    WorkspaceView& View() override { return Surface; }
    void Place(EditorUiFeature& window) override;

private:
    void DefineTransportActions();
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

    // How the views share the content area: editor.view.arrangement, so the
    // choice outlives the session.
    [[nodiscard]] ViewportArrangementKind Arrangement() const;
    void SetArrangement(ViewportArrangementKind kind);
    // Makes the view looking along `orientation` the active one, which is the
    // one a single view shows: each keeps its own camera.
    void ShowView(ViewportOrientation orientation);

    // Bake-to-static-mesh actions behind the ToolPropertiesPanel buttons. All need a
    // mounted project (the .smesh is written under its first content root).
    void ExportSelectionGlb();

    ViewsPanel* Views = nullptr;
    // Held for the Active Material panel's Browse jump (Reveal()).
    MaterialPickerPanel* Browser = nullptr;
    // Owned by the engine renderer; the views are drawn from its targets.
    EditorRenderFeature* RenderFeature = nullptr;
    // The views on the window's content surface; gone before RenderFeature.
    std::unique_ptr<LevelViewSurface> ViewSurface;
    // Whichever wheel is open, over everything in the window.
    std::unique_ptr<RadialMenuScreen> WheelScreen;
    // The window this workspace is placed in; null until it is.
    EditorUiFeature* UiFeature = nullptr;
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
    // The held-key radial menus: the tools over the workspace's registry, the
    // gizmo modes over the manipulator session. One mechanism, two models.
    std::unique_ptr<ToolRegistryMenuModel> ToolMenu;
    std::unique_ptr<RadialMenuSession> ToolWheel;
    std::unique_ptr<TransformModeMenuModel> GizmoMenu;
    std::unique_ptr<RadialMenuSession> GizmoWheel;
    std::unique_ptr<InputRouter> Router;
    std::unique_ptr<ViewportNavigation> Navigation;
    ActionCatalog& Catalog;
    ActionContext ActionHandlers;
    // The tool in hand's settings, as the operation facet hands them out.
    std::unique_ptr<ToolParameterSource> ToolParameters;
    MaterialLibrary* Materials = nullptr;
    // Thumbnail GPU residency for the browser and active-material previews.
    // Reset explicitly in the destructor after the render feature releases its
    // scene resources and before Assets goes away (the bindings inside release
    // through the asset system and the live ImGui backend).
    std::unique_ptr<MaterialThumbnailCache> Thumbnails;

    // Component identities the module's registration added serializers for,
    // so retraction removes exactly those.
    std::vector<ComponentTypeId> GameModuleSerializerTypes;

    ProjectDescriptor* Project = nullptr;

    // Declared last so they are torn down before the state they reference.
    // Cooking, the player it feeds, and the serials that hand one to the other.
    std::unique_ptr<EditorCookRuntime>  CookRuntime;
    // Kyusu's authored workflows. Owned here rather than by the UI feature
    // because they are editor logic that happens to present through a document,
    // not panels.
    //
    // One surface between them: focus and modality are arbitrated within a
    // surface, so a surface each would mean the profile dialog taking focus
    // from nothing while the inspector kept taking clicks behind it.
    UiSurfaceId                         AuthoredSurface;
    std::unique_ptr<CookProfilesModal>  ProfilesModal;
    std::unique_ptr<InspectorSurface>   Inspector;
    std::unique_ptr<DocumentFileActions> Files;
    // Declared last: its panels and chrome reference everything above.
    WorkspaceView Surface;
};
