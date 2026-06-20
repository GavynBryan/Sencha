#pragma once

#include "FreeCamera.h"

#include <app/Game.h>
#include <core/assets/AssetPreloader.h>
#include <core/assets/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <zone/AsyncZoneLoader.h>

#include <optional>
#include <string_view>

class Registry;

//=============================================================================
// TemplateGame
//
// The starting point for a Sencha game: a v4 game module (the module-is-a-Game
// contract) that mounts authored assets plus the cooked overlay and renders the
// cooked scene named by `+map levels/<name>` with a fly-camera. It boots no
// gameplay of its own. Replace the map-viewing body with your systems and
// components; the host `app` and the editor load this like any game module.
//=============================================================================
class TemplateGame final : public Game
{
public:
    void OnRegisterComponents(ComponentSerializerRegistry& serializers) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    ConsoleResult LoadMap(std::string_view mapName);
    void SetRelativeMouseMode(bool enabled);
    RuntimeAssets& RuntimeAssetState();

    Registry* ActiveZoneRegistry = nullptr;
    bool ZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::optional<AsyncZoneLoader> ZoneLoader;
    FreeCamera FreeCam;
};
