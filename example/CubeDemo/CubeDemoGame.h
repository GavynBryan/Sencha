#pragma once

#include "CubeDemoScene.h"
#include "FreeCamera.h"

#include <app/Game.h>
#include <render/Material.h>
#include <render/MeshService.h>

#include <memory>

#ifdef SENCHA_ENABLE_DEBUG_UI
class ImGuiDebugOverlay;
#endif

class CubeDemoGame final : public Game
{
public:
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    void SetRelativeMouseMode(Engine& engine, bool enabled);

    Registry* DemoRegistry = nullptr;
    MaterialStore Materials;
    std::unique_ptr<MeshService> Meshes;
    FreeCamera FreeCam;
    DemoScene Demo;

#ifdef SENCHA_ENABLE_DEBUG_UI
    ImGuiDebugOverlay* DebugOverlay = nullptr;
#endif
};
