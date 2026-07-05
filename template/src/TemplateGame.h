#pragma once

#include <app/Game.h>
#include <core/assets/AssetPreloader.h>
#include <core/assets/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <ecs/EntityId.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneId.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Registry;
class CollisionShapeCache;
class ZoneRuntime;

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
    void OnUnregisterComponents(ComponentSerializerRegistry& serializers) override;
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

    Registry* ActiveZoneRegistry = nullptr;
    bool ZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::optional<AsyncZoneLoader> ZoneLoader;
    // The world-streaming path (`world <name>`): game-owned and game-pumped by
    // WorldPartitionUpdateSystem. Empty while the single-zone `map` path runs.
    std::optional<WorldPartitionRuntime> Partition;
    // Active world-state tags gating RequiredTags transitions; the `worldtag`
    // command toggles entries. A real game pushes these from its quest/save
    // system instead.
    std::vector<std::string> WorldTags;
    ZoneId PendingZoneFocus;   // `zone <hexid>` issued before the world loaded
    EntityId WorldPawn;        // the avatar in the global registry (world path)
    // The world scene's cooked collision sidecar when the scene loaded before
    // OnRegisterSystems handed out the collision cache (a startup +world);
    // that hook loads it into the global registry and clears this.
    std::string PendingWorldSceneCollision;
    ZoneRuntime* ZoneRuntimePtr = nullptr;
    // Cooked-collision cache owned (by value) by the engine's PhysicsStepSystem;
    // grabbed at system registration so map load can fill it (LoadZoneCollision).
    // Not owned: the step system outlives the game's map loads.
    CollisionShapeCache* PhysicsShapes = nullptr;
};
