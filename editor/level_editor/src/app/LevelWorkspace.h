#pragma once

#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <ecs/ComponentTypeId.h>
#include <platform/WindowTypes.h>
#include <ui/UiSurface.h>

#include "commands/CommandStack.h"
#include "input/InputRouter.h"
#include "input/ShortcutRegistry.h"
#include "input/ViewportNavigation.h"
#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"
#include "tools/ToolRegistryMenuModel.h"
#include "editmodes/TransformModeMenuModel.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/LevelDocumentSource.h"
#include "ui/DocumentShellActions.h"
#include "project/MaterialLibrary.h"
#include "project/Project.h"
#include "ui/EditorStatusBar.h"
#include "ui/ToolPalettePanel.h"
#include "ui/EditorToolbar.h"
#include "ui/CookPlayControls.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <memory>
#include <optional>
#include <vector>

class EditorUiFeature;
class MaterialPickerPanel;
class MaterialThumbnailCache;
class ViewportPanel;
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
                   DocumentSourceSet& documents);
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
    WorkspaceView& View() override { return Surface; }
    void Place(EditorUiFeature& window) override;

private:
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
    // disk; false, with the save report shown, when one could not be saved.
    [[nodiscard]] bool SaveBeforeLaunch(std::string& error);
    // The tool wheel while it is open, painted over the whole window.
    // A radial menu while it is open, painted over the whole window.
    void DrawRadialMenu(const RadialMenuSession& wheel, const IRadialMenuModel& menu);

    // The game module's components into the editor's serializer registry, and
    // its vocabulary into every document's World, before the document exists.
    void RegisterModuleComponents(Game* module);
    // Retracted while the module is still mapped: it built the serializers.
    void RetractModuleComponents();

    void BuildAuthoredWorkflows();

    // Bake-to-static-mesh actions behind the ToolPropertiesPanel buttons. All need a
    // mounted project (the .smesh is written under its first content root).
    void ExportSelectionGlb();

    ViewportPanel* PerspectivePanel = nullptr;
    ViewportPanel* OrthoPanel = nullptr;
    // Held for the Active Material panel's Browse jump (Reveal()).
    MaterialPickerPanel* Browser = nullptr;
    // Owned by the engine renderer; kept here so BuildUi can hand its viewport target
    // cache to ViewportPanel (the panel composites those targets via ImGui::Image).
    EditorRenderFeature* RenderFeature = nullptr;
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
    UnsavedDocumentPrompt ChangesPrompt;
    bool ShowSaveReport = false;
    std::string SettleError;
    // The held-key radial menus: the tools over the workspace's registry, the
    // gizmo modes over the manipulator session. One mechanism, two models.
    std::unique_ptr<ToolRegistryMenuModel> ToolMenu;
    std::unique_ptr<RadialMenuSession> ToolWheel;
    std::unique_ptr<TransformModeMenuModel> GizmoMenu;
    std::unique_ptr<RadialMenuSession> GizmoWheel;
    std::unique_ptr<InputRouter> Router;
    std::unique_ptr<ViewportNavigation> Navigation;
    std::unique_ptr<ShortcutRegistry> Shortcuts;
    // Declared after Workspace so they are destroyed before the state they
    // reference (ToolRegistry/MeshEdit/Layout/Selection live in Workspace).
    std::unique_ptr<EditorToolbar> Toolbar;
    // The cook/play loop, mounted on the window's tab strip.
    std::unique_ptr<CookPlayControls> CookPlay;
    std::unique_ptr<EditorStatusBar> StatusBar;
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
