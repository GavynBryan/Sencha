#pragma once

#include <app/Game.h>
#include <app/GameModuleLoader.h>

#include "../commands/CommandStack.h"
#include "../input/InputEvent.h"
#include "../input/InputRouter.h"
#include "../input/ShortcutRegistry.h"
#include "../input/ViewportNavigation.h"
#include "../level/LevelWorkspace.h"
#include "../level/MaterialLibrary.h"
#include "../project/PieSession.h"
#include "../project/Project.h"
#include "../ui/EditorStatusBar.h"
#include "../ui/EditorToolbar.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EditorUiFeature;
class EditorConsolePanel;
class ViewportPanel;
class EditorViewportCameraSystem;
class EditorFrameHook;
class Engine;
class SdlWindow;

class EditorApp : public Game
{
public:
    ~EditorApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    enum class FileActionKind
    {
        Open,
        SaveAs,
    };

    struct PendingFileAction
    {
        FileActionKind Kind;
        std::string Path;
    };

    void SetRelativeMouseMode(Engine& engine, bool enabled);
    static ModifierFlags ReadModifiers(SDL_Keymod mod);
    static std::optional<InputEvent> TranslateEvent(const SDL_Event& event);
    // Resolves and stamps a translated pointer event's origin viewport before it is
    // routed: the captured viewport while a gesture holds the pointer, otherwise the
    // viewport under the cursor (which also becomes the focused viewport).
    void StampOriginViewport(InputEvent& event);

    void NewDocument();
    void SaveDocument();
    void RequestOpenDialog();
    void RequestSaveAsDialog();
    void EnqueueFileAction(FileActionKind kind, std::string path);
    void ProcessPendingFileActions();
    void RescanMaterials(const std::string& levelPath);
    void ResetEditorState();
    void UpdateWindowTitle();
    void OnFrame();

    // Opens the project (SENCHA_PROJECT = path to a .senchaproj) and loads its
    // game module so its components register into the editor's serializer registry
    // before the document is created. Falls back to a bare module path in
    // SENCHA_GAME_MODULE when no project is set.
    void LoadGameModule();
    void UnloadGameModule();

    // Registers the `play [map]` / `stop` console commands that drive PIE.
    void RegisterPlayCommands();
    // Resolves the prebuilt `app` host beside the editor executable.
    [[nodiscard]] std::string ResolveHostAppPath() const;

    ViewportPanel* Viewports = nullptr;
    EditorConsolePanel* ConsolePanel = nullptr;
    EditorUiFeature* UiFeature = nullptr;
    EditorViewportCameraSystem* CameraSystem = nullptr;
    EditorFrameHook* FrameHook = nullptr;
    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;

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

    std::mutex PendingFileMutex;
    std::vector<PendingFileAction> PendingFileActions;
    std::string LastWindowTitle;

    GameModuleLoader ModuleLoader;
    LoadedModule     GameModule;

    std::optional<ProjectDescriptor> Project;
    PieSession                       Pie;
};
