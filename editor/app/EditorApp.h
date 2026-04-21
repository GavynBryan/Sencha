#pragma once

#include <app/Game.h>

#include "../commands/CommandStack.h"
#include "../input/InputEvent.h"
#include "../input/InputRouter.h"
#include "../input/ShortcutRegistry.h"
#include "../input/ViewportNavigation.h"
#include "../level/LevelWorkspace.h"

#include <memory>
#include <optional>

class EditorUiFeature;
class ViewportPanel;
class EditorViewportCameraSystem;
class Engine;

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
    void SetRelativeMouseMode(Engine& engine, bool enabled);
    static ModifierFlags ReadModifiers(SDL_Keymod mod);
    static std::optional<InputEvent> TranslateEvent(const SDL_Event& event);

    ViewportPanel* Viewports = nullptr;
    EditorUiFeature* UiFeature = nullptr;
    EditorViewportCameraSystem* CameraSystem = nullptr;
    Engine* EnginePtr = nullptr;

    std::unique_ptr<CommandStack> Commands;
    std::unique_ptr<LevelWorkspace> Workspace;
    std::unique_ptr<InputRouter> Router;
    std::unique_ptr<ViewportNavigation> Navigation;
    std::unique_ptr<ShortcutRegistry> Shortcuts;
};
