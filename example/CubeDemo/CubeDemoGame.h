#pragma once

#include "CubeDemoScene.h"
#include "FreeCamera.h"

#include <app/Game.h>
#include <core/assets/AssetPreloader.h>
#include <core/assets/RuntimeAssets.h>
#include <zone/AsyncZoneLoader.h>

#include <optional>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/cook/TextureCook.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
#endif

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
    RuntimeAssets& RuntimeAssetState();
    const RuntimeAssets& RuntimeAssetState() const;

    // Null until the async zone load commits; systems and the debug panel
    // null-check it every tick, so the demo runs (and renders nothing from
    // the zone) while the load is in flight.
    Registry* DemoRegistry = nullptr;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::optional<AsyncZoneLoader> ZoneLoader;
    FreeCamera FreeCam;
    DemoScene Demo;

#ifdef SENCHA_ENABLE_COOK
    // Dev-only asset hot reload (Stage 6a, textures): the importer the watcher
    // re-cooks through (held here so the non-owning registry stays valid), the
    // source-change detector, and the reload driver. Ticked (throttled) by a
    // per-frame system; see OnRegisterSystems. 6b/6c add the mesh/material
    // importers + watched extensions.
    PngTextureImporter HotReloadPngImporter;
    AssetImporterRegistry HotReloadImporters;
    std::optional<AssetSourceWatcher> Watcher;
    std::optional<AssetHotReloader> Reloader;
#endif

#ifdef SENCHA_ENABLE_DEBUG_UI
    ImGuiDebugOverlay* DebugOverlay = nullptr;
#endif
};
