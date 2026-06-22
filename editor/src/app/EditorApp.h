#pragma once

#include <app/Game.h>
#include <app/GameModuleLoader.h>
#include <core/assets/RuntimeAssets.h>

#include "../commands/CommandStack.h"
#include "../input/InputRouter.h"
#include "../input/ShortcutRegistry.h"
#include "../input/ViewportNavigation.h"
#include "../level/LevelWorkspace.h"
#include "../level/MaterialLibrary.h"
#include "../project/Project.h"
#include "../ui/EditorStatusBar.h"
#include "../ui/EditorToolbar.h"

#include <memory>
#include <optional>
#include <string>

class EditorUiFeature;
class EditorConsolePanel;
class ViewportPanel;
class EditorViewportCameraSystem;
class EditorFrameHook;
class Engine;
class SdlWindow;
class PieDriver;
class DocumentFileActions;

class EditorApp : public Game
{
public:
    // Defined out of line (the .cpp sees the complete PieDriver / DocumentFileActions
    // the unique_ptr members forward-declare): an in-class default would instantiate
    // their destruction at every construction site with the types still incomplete.
    EditorApp();
    ~EditorApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    void OnFrame();

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

    ViewportPanel* Viewports = nullptr;
    EditorConsolePanel* ConsolePanel = nullptr;
    EditorUiFeature* UiFeature = nullptr;
    EditorViewportCameraSystem* CameraSystem = nullptr;
    EditorFrameHook* FrameHook = nullptr;
    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;

    // The engine asset system, shared by editor authoring, the cook, and (by the
    // same paths) the runtime. Declared before Workspace so it outlives the
    // document whose StaticMeshComponents hold handles into its caches. Reset in
    // OnShutdown before the engine frees the graphics services it borrows.
    std::optional<RuntimeAssets> Assets;

    std::unique_ptr<CommandStack> Commands;
    std::unique_ptr<LevelWorkspace> Workspace;
    std::unique_ptr<InputRouter> Router;
    std::unique_ptr<ViewportNavigation> Navigation;
    std::unique_ptr<ShortcutRegistry> Shortcuts;
    // Declared after Workspace so they are destroyed before the state they
    // reference (ToolRegistry/MeshEdit/Layout/Selection live in Workspace).
    std::unique_ptr<EditorToolbar> Toolbar;
    std::unique_ptr<EditorStatusBar> StatusBar;
    std::unique_ptr<MaterialLibrary> Materials;

    GameModuleLoader ModuleLoader;
    LoadedModule     GameModule;

    std::optional<ProjectDescriptor> Project;

    // Declared last so they are torn down before the state they reference: Pie
    // holds the document and project; Files holds the document, command stack,
    // selection, and material library.
    std::unique_ptr<PieDriver>          Pie;
    std::unique_ptr<DocumentFileActions> Files;
};
