#pragma once

#include <app/GameContexts.h>
#include <app/GameModuleLoader.h>
#include <core/assets/RuntimeAssets.h>

#include "commands/CommandStack.h"
#include "input/InputRouter.h"
#include "input/ShortcutRegistry.h"
#include "input/ViewportNavigation.h"
#include "workspace/EditorWorkspace.h"
#include "project/MaterialLibrary.h"
#include "project/Project.h"
#include "ui/EditorStatusBar.h"
#include "ui/EditorToolSidebar.h"
#include "ui/EditorToolbar.h"

#include <memory>
#include <optional>

class EditorUiFeature;
class EditorConsolePanel;
class MaterialPanel;
class ViewportPanel;
class EditorRenderFeature;
class EditorViewportCameraSystem;
class EditorFrameHook;
class Engine;
class EngineSchedule;
class SdlWindow;
class PieDriver;
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
    // Builds and wires every subsystem. Defined out of line so the .cpp sees the
    // complete PieDriver / DocumentFileActions the unique_ptr members forward-
    // declare. config supplies startup-only settings (console open-on-start);
    // projectPath is the resolved .senchaproj path (ResolveProjectPath), if any.
    EditorServices(Engine& engine,
                   SdlWindow& window,
                   const EngineConfig& config,
                   std::optional<std::string> projectPath);
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

    // Opens the project (SENCHA_PROJECT = path to a .senchaproj) and loads its
    // game module so its components register into the editor's serializer registry
    // before the document is created. Falls back to a bare module path in
    // SENCHA_GAME_MODULE when no project is set.
    void LoadGameModule();
    void UnloadGameModule();

    // Builds the engine asset system from the live graphics services and mounts the
    // project's content roots (authored + cooked overlay) so asset refs resolve as
    // they do at runtime. No-op without a project.
    void InitAssets();

    // Watches project .smat/.png sources and hot-reloads resident assets in
    // place (detection: AssetSourceWatcher; reaction: AssetHotReloader), so a
    // save from the material editor or a text editor shows up live. No-op
    // without a mounted project.
    void BuildSourceWatch();

    // Bake-to-static-mesh actions behind the MeshEditPanel buttons. All need a
    // mounted project (the .smesh is written under its first content root).
    void BakeSelectedBrushes();
    void RevertSelectedBakedBrushes();
    void ExportSelectionGlb();
    [[nodiscard]] bool SelectionHasBakedBrush() const;

    ViewportPanel* Viewports = nullptr;
    // Owned by the engine renderer; kept here so BuildUi can hand its viewport target
    // cache to ViewportPanel (the panel composites those targets via ImGui::Image).
    EditorRenderFeature* RenderFeature = nullptr;
    EditorConsolePanel* ConsolePanel = nullptr;
    // Held for the copy/paste-projection shortcuts (the panel owns the stash).
    MaterialPanel* MaterialsPanel = nullptr;
    EditorUiFeature* UiFeature = nullptr;
    EditorViewportCameraSystem* CameraSystem = nullptr;
    EditorFrameHook* FrameHook = nullptr;
    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;

    // The engine asset system, shared by editor authoring, the cook, and (by the
    // same paths) the runtime. Declared before Workspace so it outlives the
    // document whose StaticMeshComponents hold handles into its caches. Reset in
    // the destructor before the engine frees the graphics services it borrows.
    std::optional<RuntimeAssets> Assets;

    // Source watch state (definition in the .cpp keeps the cook/hotreload
    // headers out of this one). References Assets; reset before it.
    struct SourceWatchState;
    std::unique_ptr<SourceWatchState> SourceWatch;

    std::unique_ptr<CommandStack> Commands;
    std::unique_ptr<EditorWorkspace> Workspace;
    std::unique_ptr<InputRouter> Router;
    std::unique_ptr<ViewportNavigation> Navigation;
    std::unique_ptr<ShortcutRegistry> Shortcuts;
    // Declared after Workspace so they are destroyed before the state they
    // reference (ToolRegistry/MeshEdit/Layout/Selection live in Workspace).
    std::unique_ptr<EditorToolbar> Toolbar;
    std::unique_ptr<EditorToolSidebar> ToolSidebar;
    std::unique_ptr<EditorStatusBar> StatusBar;
    std::unique_ptr<MaterialLibrary> Materials;

    GameModuleLoader ModuleLoader;
    LoadedModule     GameModule;

    std::optional<std::string> ProjectPath;
    std::optional<ProjectDescriptor> Project;

    // Declared last so they are torn down before the state they reference: Pie
    // holds the document and project; Files holds the document, command stack,
    // selection, and material library.
    std::unique_ptr<PieDriver>          Pie;
    std::unique_ptr<DocumentFileActions> Files;
};
