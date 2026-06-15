#pragma once

#include <app/Game.h>
#include <app/GameModuleLoader.h>

#include "../commands/CommandStack.h"
#include "../input/InputEvent.h"
#include "../input/InputRouter.h"
#include "../input/ShortcutRegistry.h"
#include "../input/ViewportNavigation.h"
#include "../level/LevelWorkspace.h"

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

    void NewDocument();
    void SaveDocument();
    void RequestOpenDialog();
    void RequestSaveAsDialog();
    void EnqueueFileAction(FileActionKind kind, std::string path);
    void ProcessPendingFileActions();
    void ResetEditorState();
    void UpdateWindowTitle();
    void OnFrame();

    // Loads the project's game module (path from the SENCHA_GAME_MODULE env var
    // for now; an EditorProject will own it later) so its components register into
    // the editor's serializer registry before the document is created.
    void LoadGameModule();
    void UnloadGameModule();

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

    std::mutex PendingFileMutex;
    std::vector<PendingFileAction> PendingFileActions;
    std::string LastWindowTitle;

    GameModuleLoader ModuleLoader;
    LoadedModule     GameModule;
    EngineHostInfo   HostInfo;
};
