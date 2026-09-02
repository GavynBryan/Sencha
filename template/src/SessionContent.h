#pragma once

#include "PlayerAvatarData.h"

#include <assets/data/DataAssetHandle.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <input/InputContextSet.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneId.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
#endif

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
// Everything this run has loaded and everything it took to load it: the asset
// stack composed for what this process can hold, the preloader and zone loader
// over it, the world partition when a world is up, the game's own authored
// data, and the references all of that holds.
//
// It exists because those are one lifetime, not several. A reference into the
// asset stack that outlives the stack calls through a destroyed vtable at
// shutdown -- which is a crash on the way out rather than at the mistake -- so
// the order in which they are given back is a property worth having one owner
// for. Open composes, Close gives back, and the members are declared so that
// destruction alone would do the same thing.
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

    // Composes the asset stack this process can hold, scans the content roots,
    // wires the world's resources and the services that resolve scenes through
    // it, and binds the controls.
    void Open();

    // Gives back everything held into the asset stack, then drops it. Explicit
    // rather than left to the destructor because the data subtype registrations
    // hold function pointers into this module and have to go while it is still
    // mapped, and because the world it detaches outlives this object.
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
    // Invalid on a process that cannot hold a mesh, which is what a bodyless
    // pawn wants rather than a failure.
    [[nodiscard]] ResolvedPlayerAvatar PlayerAvatar();

    // The composed asset stack, for the startup wiring that points engine
    // services and debug panels at it.
    [[nodiscard]] RuntimeAssets& Assets();

private:
    [[nodiscard]] DataAssetCacheHandle AcquireDataAsset(std::string_view path);
    void ReleasePlayerAvatar();
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
    // handles and leases into the asset stack go before the stack does, and
    // the loaders over it go before both.
    std::optional<RuntimeAssets> Assets_;
    std::optional<AssetPreloader> Preloader;
    std::unique_ptr<SceneSerializationContext> SceneContext;
    std::optional<AsyncZoneLoader> ZoneLoader;
    std::optional<WorldPartitionRuntime> Partition;

#ifdef SENCHA_ENABLE_COOK
    // Dev-only source watching so authored data (movement tuning) reloads in
    // place while the game runs. No importers: .sdata is a runtime format.
    AssetImporterRegistry HotReloadImporters;
    std::optional<AssetHotReloader> HotReloader;
    std::optional<AssetSourceWatcher> HotReloadWatcher;
#endif

    // Held for the run, released in Close before the caches are destroyed.
    DataAssetCacheHandle PlayerAvatarAsset;
    DataAssetCacheHandle GameSettingsAsset;
    DataAssetCacheHandle InputActionSetAsset;
    DataAssetCacheHandle InputProfileAsset;
    // Resolved once so spawning a second pawn does not reload the body.
    ResolvedPlayerAvatar Avatar;
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
