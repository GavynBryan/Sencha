#pragma once

#include <app/Game.h>
#include <assets/data/DataAssetHandle.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <ecs/EntityId.h>
#include <input/InputAction.h>
#include <input/InputContextSet.h>
#include <movement/MovementProfileData.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
#endif
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

// The actions this game reads, resolved from the profile's action set once at
// startup. Systems index by id from here; adding an action is an edit to
// input_actions.sdata, a binding in the profile, and one field here.
struct TemplateInputActions
{
    InputActionId Move;
    InputActionId Look;
    InputActionId Jump;
};

class TemplateGame final : public Game
{
public:
    void OnRegisterComponents(
        ComponentSerializerRegistry& serializers) override;
    void OnUnregisterComponents(
        ComponentSerializerRegistry& serializers) override;
    void OnRegisterRuntimeComponents(
        WorldComponentSchema& schema) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    ConsoleResult LoadMap(std::string_view mapName);
    ConsoleResult LoadWorld(std::string_view worldName);
    ConsoleResult FocusWorldZone(std::string_view zoneHex);
    void SetRelativeMouseMode(bool enabled);
    RuntimeAssets& RuntimeAssetState();
    DataAssetCacheHandle AcquireDataAsset(std::string_view path, Logger& log);
    MovementProfileHandle ResolvePlayerMovementProfile(Logger& log);
    void SetupInputMapping(Logger& log);

    bool PlayZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::unique_ptr<SceneSerializationContext> SceneContext;
    std::optional<AsyncZoneLoader> ZoneLoader;
    std::optional<WorldPartitionRuntime> Partition;
    ZoneId PendingZoneFocus;
    EntityId PlayerPawn;
    // Declared after Assets so their release runs before the cache is destroyed.
    DataAssetCacheHandle PlayerMovementProfile;
    DataAssetCacheHandle InputActionSetAsset;
    DataAssetCacheHandle InputProfileAsset;
    // Held for the process: this game is always in its gameplay context. A
    // menu would take its own lease and drop this one.
    InputContextLease GameplayInput;
    std::string PendingWorldSceneCollision;
    CollisionShapeCache* PhysicsShapes = nullptr;

#ifdef SENCHA_ENABLE_COOK
    // Dev-only source watching so authored data (movement tuning) reloads
    // in place while the game runs. No importers: .sdata is a runtime format.
    AssetImporterRegistry HotReloadImporters;
    std::optional<AssetHotReloader> HotReloader;
    std::optional<AssetSourceWatcher> HotReloadWatcher;
#endif
};
