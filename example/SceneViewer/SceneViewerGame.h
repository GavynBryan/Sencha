#pragma once

#include "FreeCamera.h"

#include <input/InputContextSet.h>

#include <app/Game.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <ecs/EntityId.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>

#include <memory>
#include <optional>
#include <string_view>

//=============================================================================
// SceneViewerGame
//
// A minimal game module (the v4 module-is-a-Game contract): it boots no
// gameplay, only mounts authored assets plus the cooked overlay and renders the
// cooked scene named by the `map` console command (`+map levels/foo`) with a
// fly-camera. The host `app` loads it like any game module. It is the "empty
// game" / pure viewer: real games supply their own systems and camera; the
// editor spawns this (or a real game module) for Play-In-Editor.
//=============================================================================
class SceneViewerGame final : public Game
{
public:
    void OnRegisterComponents(ComponentRegistrar& registrar) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    ConsoleResult LoadMap(std::string_view mapName);
    void SetRelativeMouseMode(bool enabled);
    RuntimeAssets& RuntimeAssetState();

    bool ZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::unique_ptr<SceneSerializationContext> SceneContext;
    std::optional<AsyncZoneLoader> ZoneLoader;
    EntityId CameraEntity;
    FreeCamera FreeCam;
    // Held for the whole session: the viewer is always in its fly context.
    // Released in OnShutdown, not by this Game's destructor -- the module's
    // static instance is destroyed at dlclose, after the InputContextSet the
    // lease detaches from is gone.
    InputContextLease FlyInput;
    // Armed by sceneviewer.camera.scripted; read by the scripted-path system.
    bool ScriptedCameraEnabled = false;
};
