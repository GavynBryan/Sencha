#pragma once

#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <ecs/ComponentTypeId.h>
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
#include "project/MaterialLibrary.h"
#include "project/Project.h"
#include "ui/EditorStatusBar.h"
#include "ui/ToolPalettePanel.h"
#include "ui/EditorToolbar.h"
#include "ui/WorkspaceBar.h"

#include <memory>
#include <optional>
#include <vector>

class SourceReloadRoots;
class EditorUiFeature;
class EditorConsolePanel;
class MaterialPickerPanel;
class MaterialThumbnailCache;
class ViewportPanel;
class EditorRenderFeature;
class EditorViewportCameraSystem;
class EditorFrameHook;
class Engine;
class Game;
class EngineSchedule;
class SdlWindow;
class PieDriver;
class CookSession;
class CookProfilesModal;
class InspectorSurface;
class CookProfilesPanel;
class EditorCookRuntime;
class DocumentFileActions;

//=============================================================================
// EditorServices
//
// The editor's subsystems, owned and wired as a group against the live engine
// and primary window. The constructor mounts the project, builds the asset
// system, creates the document/command/input/UI subsystems, and registers the
// editor's render and UI features on the engine renderer: it is where the editor
// is composed. EditorApp holds one behind a unique_ptr and forwards the Game
// lifecycle hooks to it, so EditorApp stays glue.
//
// Member order is dependency order. The destructor reproduces the load-bearing
// teardown sequence explicitly: Pie and Files reference document/command state so
// they go first; Assets must outlive the document (whose StaticMeshComponents
// hold handles into its caches) yet be released before the engine frees the
// graphics services those caches borrow.
//=============================================================================
class EditorServices
{
public:
    // Builds and wires every subsystem. config supplies startup-only settings
    // (console open-on-start). The project, the game module and the materials
    // belong to the application and outlive this.
    EditorServices(Engine& engine,
                   SdlWindow& window,
                   const EngineConfig& config,
                   ProjectDescriptor* project,
                   Game* module,
                   MaterialLibrary& materials);
    ~EditorServices();

    EditorServices(const EditorServices&) = delete;
    EditorServices& operator=(const EditorServices&) = delete;

    // Registers the editor's frame systems (viewport camera + the per-frame hook).
    void RegisterSystems(EngineSchedule& schedule);

    // Routes one platform event: window resize, console toggle, ImGui
    // preprocessing, then the input router chain.
    void HandlePlatformEvent(PlatformEventContext& ctx);

private:
    // Constructor phases, in call order. Each builds one cohesive slice of the
    // editor against the engine (*EnginePtr) and primary window (*Window), so the
    // constructor reads as the bring-up sequence.
    void BuildDocument();
    void BuildPlayLoop();
    void BuildFileActions();
    void BuildInput();
    void BuildViewportRendering();
    void BuildUi(bool consoleOpenOnStart);

    void ProcessFrame();
    // The tool wheel while it is open, painted over the whole window.
    // A radial menu while it is open, painted over the whole window.
    void DrawRadialMenu(const RadialMenuSession& wheel, const IRadialMenuModel& menu);

    // The game module's components into the editor's serializer registry, and
    // its vocabulary into every document's World, before the document exists.
    void RegisterModuleComponents(Game* module);
    // Retracted while the module is still mapped: it built the serializers.
    void RetractModuleComponents();

    // Watches project .smat/.png sources and hot-reloads resident assets in
    // place (detection: AssetSourceWatcher; reaction: AssetHotReloader), so a
    // save from the material editor or a text editor shows up live. No-op
    // without a mounted project.
    void BuildAuthoredWorkflows();
    void BuildSourceWatch();

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
    EditorConsolePanel* ConsolePanel = nullptr;
    EditorUiFeature* UiFeature = nullptr;
    EditorViewportCameraSystem* CameraSystem = nullptr;
    EditorFrameHook* FrameHook = nullptr;
    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;

    // The engine's asset stack, which the application mounted the project into.
    RuntimeAssets* Assets = nullptr;

    // Source watch state (definition in the .cpp keeps the cook/hotreload
    // headers out of this one). References Assets; reset before it.
    std::unique_ptr<SourceReloadRoots> SourceWatch;

    std::unique_ptr<CommandStack> Commands;
    std::unique_ptr<EditorWorkspace> Workspace;
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
    // The bar under the caption: the cook/play loop, and workspace tabs to come.
    std::unique_ptr<WorkspaceBar> TopBar;
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
};
