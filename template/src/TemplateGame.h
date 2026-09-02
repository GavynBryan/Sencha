#pragma once

#include <app/Game.h>
#include <assets/data/DataAssetHandle.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <ecs/EntityId.h>
#include <input/InputContextSet.h>
#include <movement/MovementProfileData.h>

#include "GameSettingsData.h"
#include "PlayerAvatarData.h"

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
#endif
#include <world/scene/SmapFormat.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneId.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CollisionShapeCache;
struct ProbeVolumeFile;
struct RuntimeZoneRecord;

class TemplateGame final : public Game
{
public:
    void OnRegisterComponents(ComponentRegistrar& registrar) override;
    void OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                  DataSchemaRegistry& schemas) override;
    void OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                    DataSchemaRegistry& schemas) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    ConsoleResult LoadMap(std::string_view mapName);
    ConsoleResult LoadWorld(std::string_view worldName);
    // The shared cooked-content attach for streamed scenes (+map and world
    // zones): collision cells and the sibling probe file.
    void AttachStreamedSceneContent(RuntimeWorld& runtime,
                                    RuntimeZoneRecord& zone,
                                    const SmapContents& contents,
                                    const ProbeVolumeFile& probes);
    [[nodiscard]] static AsyncZoneLoader::SceneStageFn MakeProbeStage(
        std::string sceneFilePath, std::shared_ptr<ProbeVolumeFile> probes);
    ConsoleResult FocusWorldZone(std::string_view zoneHex);
    ConsoleResult SetCameraMode(std::string_view modeName);
    ConsoleResult RequestTurret(bool placeOnly);
    void SetRelativeMouseMode(bool enabled);
    RuntimeAssets& RuntimeAssetState();
    DataAssetCacheHandle AcquireDataAsset(std::string_view path, Logger& log);
    ResolvedPlayerAvatar ResolvePlayerAvatar(Logger& log);
    // Read at every spawn request, never cached as a struct: a hot reload
    // swaps the compiled value under the token and the next spawn sees it.
    const CompiledGameSettings* ResolveGameSettings(Logger& log);
    void ReleasePlayerAvatar();
    void SetupInputMapping(Logger& log);

    bool PlayZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::unique_ptr<SceneSerializationContext> SceneContext;
    std::optional<AsyncZoneLoader> ZoneLoader;
    std::optional<WorldPartitionRuntime> Partition;
    ZoneId PendingZoneFocus;
    // Declared after Assets so their release runs before the cache is destroyed.
    DataAssetCacheHandle PlayerAvatarAsset;
    DataAssetCacheHandle GameSettingsAsset;
    DataAssetCacheHandle InputActionSetAsset;
    DataAssetCacheHandle InputProfileAsset;
    // Resolved once and held for the process so spawning a second pawn does not
    // reload the body. Released in OnShutdown, before the caches are destroyed.
    ResolvedPlayerAvatar PlayerAvatar;
    // Held for the process: this game is always in its gameplay context. A
    // menu would take its own lease and drop this one.
    InputContextLease GameplayInput;
    // The world scene's collision cells when they arrived before physics did;
    // loaded and cleared once the shape cache exists.
    std::vector<SmapCollisionCell> PendingWorldSceneCollision;
    CollisionShapeCache* PhysicsShapes = nullptr;

#ifdef SENCHA_ENABLE_COOK
    // Dev-only source watching so authored data (movement tuning) reloads
    // in place while the game runs. No importers: .sdata is a runtime format.
    AssetImporterRegistry HotReloadImporters;
    std::optional<AssetHotReloader> HotReloader;
    std::optional<AssetSourceWatcher> HotReloadWatcher;
#endif
};
