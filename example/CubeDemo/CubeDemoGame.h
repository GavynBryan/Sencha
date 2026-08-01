#pragma once

#include "CubeDemoScene.h"
#include "FreeCamera.h"

#include <app/Game.h>
#include <components/ActiveCameraService.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/AsyncZoneLoader.h>

#include <memory>
#include <optional>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/cook/BlendCook.h>
#include <assets/cook/MeshCook.h>
#include <assets/cook/TextureCook.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
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

    bool ZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::unique_ptr<SceneSerializationContext> SceneContext;
    std::optional<AsyncZoneLoader> ZoneLoader;
    FreeCamera FreeCam;
    DemoScene Demo;

#ifdef SENCHA_ENABLE_COOK
    PngTextureImporter HotReloadPngImporter;
    GltfMeshImporter HotReloadGltfImporter;
    BlendMeshImporter HotReloadBlendImporter;
    AssetImporterRegistry HotReloadImporters;
    std::optional<AssetSourceWatcher> Watcher;
    std::optional<AssetHotReloader> Reloader;
#endif

};
