#pragma once


#include <assets/data/DataAssetHandle.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <input/InputContextSet.h>
#include <world/scene/SmapFormat.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneId.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CollisionShapeCache;
class Engine;
class Logger;
struct CompiledGameSettings;
struct ProbeVolumeFile;
struct RuntimeZoneRecord;
struct SystemRegisterContext;

// The game's data vocabulary. One list, registered into whichever registries
// are asking: the session's own at startup, and the data editor's through the
// game module's OnRegisterDataAssetTypes hook.
void RegisterTemplateDataTypes(DataAssetTypeRegistry& types,
                               DataSchemaRegistry& schemas);
void UnregisterTemplateDataTypes(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas);

//=============================================================================
// SessionContent
//
// What this game has loaded on top of the engine's content: the loaders and
// world partition a level needs, the game's own authored data, and the
// references those hold.
//
// The asset stack itself is the engine's (Engine::Content), and so is the
// order in which the engine's own consumers give it back. What is left here is
// this game's half of the same rule: every lease it takes is released in Close,
// while the caches that issued them still exist.
//
// The engine and the logger are named collaborators; nothing here reaches back
// into the game object that holds it.
//=============================================================================
class SessionContent
{
public:
    SessionContent(Engine& engine, Logger& log);
    ~SessionContent();

    SessionContent(const SessionContent&) = delete;
    SessionContent& operator=(const SessionContent&) = delete;

    // Registers the gameplay features this game's world carries, composes the
    // loaders over the engine's content, and binds the controls.
    void Open();

    // Gives back everything this game holds into the engine's content stack.
    // Explicit rather than left to the destructor because the world it detaches
    // outlives this object, and because the leases have to drop before the
    // engine tears the stack down.
    void Close();

    // The systems that drive loaded content, and the shape cache the collision
    // that content carries loads into.
    void RegisterSystems(SystemRegisterContext& ctx);

    [[nodiscard]] ConsoleResult LoadMap(std::string_view mapName);
    [[nodiscard]] ConsoleResult LoadWorld(std::string_view worldName);
    [[nodiscard]] ConsoleResult FocusZone(std::string_view zoneHex);
    [[nodiscard]] ConsoleResult DescribeZones() const;

    // The authored data this game reads, loaded on first ask and held for the
    // run. Read at every use, never cached by the caller: a hot reload swaps
    // the compiled value under the token and the next ask sees it.
    [[nodiscard]] const CompiledGameSettings* GameSettings();

    // The engine's asset stack, for the startup wiring that reads this game's
    // authored data out of it.
    [[nodiscard]] RuntimeAssets& Assets();

private:
    [[nodiscard]] DataAssetCacheHandle AcquireDataAsset(std::string_view path);
    void SetupInputMapping();
    // The shared cooked-content attach for streamed scenes (+map and world
    // zones): collision cells and the sibling probe file.
    void AttachStreamedSceneContent(RuntimeWorld& runtime,
                                    RuntimeZoneRecord& zone,
                                    const SmapContents& contents,
                                    const ProbeVolumeFile& probes);
    [[nodiscard]] static AsyncZoneLoader::SceneStageFn MakeProbeStage(
        std::string sceneFilePath, std::shared_ptr<ProbeVolumeFile> probes);

    Engine& Host;
    Logger& Log;

    // Declaration order is the destruction contract, and Close mirrors it: the
    // loaders over the engine's asset stack go before the handles into it.
    std::optional<AssetPreloader> Preloader;
    std::optional<AsyncZoneLoader> ZoneLoader;
    std::optional<WorldPartitionRuntime> Partition;

    // Held for the run, released in Close before the caches are destroyed.
    DataAssetCacheHandle GameSettingsAsset;
    DataAssetCacheHandle InputActionSetAsset;
    DataAssetCacheHandle InputProfileAsset;
    // Held for the process: this game is always in its gameplay context. A
    // menu would take its own lease and drop this one.
    InputContextLease GameplayInput;

    bool PlayZoneActive = false;
    ZoneId PendingZoneFocus;
    // The world scene's collision cells when they arrived before physics did;
    // loaded and cleared once the shape cache exists.
    std::vector<SmapCollisionCell> PendingWorldSceneCollision;
    CollisionShapeCache* PhysicsShapes = nullptr;
};
