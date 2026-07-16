#pragma once

#include <app/Game.h>
#include <core/assets/AssetPreloader.h>
#include <core/assets/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <ecs/EntityId.h>
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

    bool PlayZoneActive = false;
    std::optional<RuntimeAssets> Assets;
    std::optional<AssetPreloader> Preloader;
    std::unique_ptr<SceneSerializationContext> SceneContext;
    std::optional<AsyncZoneLoader> ZoneLoader;
    std::optional<WorldPartitionRuntime> Partition;
    ZoneId PendingZoneFocus;
    EntityId PlayerPawn;
    std::string PendingWorldSceneCollision;
    CollisionShapeCache* PhysicsShapes = nullptr;
};
