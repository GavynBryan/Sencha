#pragma once

#include <assets/runtime/AssetPreloader.h>
#include <ecs/EntityId.h>
#include <core/console/ConsoleTypes.h>
#include <world/scene/SmapFormat.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneId.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CollisionShapeCache;
class Engine;
class Logger;
class RuntimeContent;
class RuntimeWorld;
struct ProbeVolumeFile;
struct RuntimeZoneRecord;

//=============================================================================
// LoadedLevel
//
// What this process has loaded: one cooked scene in the play zone, or one
// partitioned world and the runtime that streams its zones. The loaders over
// the content stack belong to it, because they are only meaningful while
// something is loaded, and they go when it unloads.
//
// It loads and publishes content, and stops there. It does not activate a
// camera, admit a participant, ask for a body, choose a streaming focus after
// the first one, or touch input. Those are decisions about what a game does
// with a loaded level, and a game makes them by watching the zone-residency
// changes the load already publishes.
//
// The play zone is a convention of the cooked format rather than a choice:
// partition zero is persistent, and a single loaded scene occupies zone one.
//=============================================================================
class LoadedLevel
{
public:
    LoadedLevel(Engine& engine, RuntimeContent& content, Logger& log);
    ~LoadedLevel();

    LoadedLevel(const LoadedLevel&) = delete;
    LoadedLevel& operator=(const LoadedLevel&) = delete;

    // One cooked scene into the play zone. Asynchronous: the zone is resident
    // when its Attached residency change is published, not when this returns.
    [[nodiscard]] ConsoleResult LoadScene(std::string_view mapName);

    // One cooked partitioned world, streamed around a focus. `initialFocus`
    // defaults to the start zone the world manifest declares; a caller that
    // knows better -- a saved position, a chosen spawn -- passes its own.
    [[nodiscard]] ConsoleResult LoadWorld(std::string_view worldName,
                                          std::optional<ZoneId> initialFocus);

    [[nodiscard]] ConsoleResult FocusZone(ZoneId zone);
    [[nodiscard]] ConsoleResult DescribeZones() const;

    // Detaches every zone this loaded, destroys the entities a world scene
    // imported, and drops the loaders. Idempotent; safe with nothing loaded.
    void Unload();

    // Where a level's collision goes. Called once the schedule is known,
    // because physics is a game's choice: a game without it never calls this
    // and the cells a level carried are dropped at unload.
    void ConnectCollision(CollisionShapeCache& shapes);

    [[nodiscard]] bool IsLoaded() const;

    // The zone a loaded scene occupies, or the world's focus zone. Invalid
    // with nothing loaded.
    [[nodiscard]] ZoneId PlayZone() const;

private:
    void AttachSceneContent(RuntimeWorld& runtime,
                            RuntimeZoneRecord& zone,
                            const SmapContents& contents,
                            const ProbeVolumeFile& probes);
    [[nodiscard]] static AsyncZoneLoader::SceneStageFn MakeProbeStage(
        std::string sceneFilePath, std::shared_ptr<ProbeVolumeFile> probes);
    // The cooked directory collision cells resolve against: the first mounted
    // root's. Levels come from one root today; when they can come from several,
    // the scene has to say which one it was loaded from.
    [[nodiscard]] std::string CookedRoot() const;

    Engine& Host;
    RuntimeContent& Content;
    Logger& Log;

    // Declaration order is the destruction contract: the loaders go before the
    // partition that drives them.
    std::optional<AssetPreloader> Preloader;
    std::optional<AsyncZoneLoader> ZoneLoader;
    std::optional<WorldPartitionRuntime> Partition;

    // True once a scene load has been accepted, so a second one is refused
    // rather than layered onto the first.
    bool SceneLoaded = false;
    // Entities a world scene imported into the persistent partition. Recorded
    // because the partition also holds whatever the game spawned there, and
    // unload owns only what it loaded.
    std::vector<EntityId> WorldSceneEntities;
    // Collision a level carried before physics existed, loaded once there is
    // somewhere to put it.
    std::vector<SmapCollisionCell> PendingCollision;
    CollisionShapeCache* PhysicsShapes = nullptr;
};
