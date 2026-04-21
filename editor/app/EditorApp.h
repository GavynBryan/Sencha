#pragma once

#include <app/Game.h>

#include "../commands/CommandStack.h"
#include "../selection/SelectionContext.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/Picking.h"
#include "../viewport/FourWayViewportLayout.h"

#include <memory>

class EditorUiFeature;
class ViewportPanel;
class ToolPalettePanel;
class EditorViewportCameraSystem;

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

    std::unique_ptr<FourWayViewportLayout> ViewportLayout;
    ViewportPanel* Viewports = nullptr;
    EditorUiFeature* UiFeature = nullptr;
    EditorViewportCameraSystem* CameraSystem = nullptr;
    std::unique_ptr<CommandStack> Commands;
    std::unique_ptr<SelectionContext> Selection;
    std::unique_ptr<SelectionService> SelectionState;
    std::unique_ptr<PickingService> Picking;
    std::unique_ptr<ToolContext> ToolState;
    std::unique_ptr<ToolRegistry> Tools;
};
