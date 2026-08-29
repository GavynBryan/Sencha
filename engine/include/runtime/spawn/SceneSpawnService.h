#pragma once

#include <core/identity/StrongId.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <jobs/AsyncTaskQueue.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class AssetSystem;
class ComponentSerializerRegistry;
class LoggingProvider;
class RuntimeWorld;
class WorldComponentSchema;
struct SmapContents;

//=============================================================================
// SceneSpawnService
//
// Runtime scene spawning: a request names a cooked scene, a root transform,
// and a storage partition; the scene stages and its package builds on the
// task lane; entities publish on the owner thread at the async drain, in
// REQUEST order, so worker completion order can never change entity
// allocation. A failed request creates nothing (the importer's rollback
// guarantee). Spawned entities carry SceneInstance with a runtime-minted id,
// so the group index is the ordinary SceneInstanceIndex -- gameplay
// destroying one entity prunes it through the component's own hooks, and the
// service holds no second index.
//
// Spawns are transient in v1: authored persistent identity is stripped, so a
// spawned scene neither collides with authored content nor participates in
// zone state memory. Entities live in their requested partition and follow
// its ordinary teardown.
//
// Owned by Engine; the game wires its asset stack once at startup
// (ConnectAssets), because RuntimeAssets is game-owned. Requests before the
// wiring fail.
//=============================================================================

using SceneSpawnId = StrongId<struct SceneSpawnIdTag, std::uint64_t>;

enum class SceneSpawnStatus : std::uint8_t
{
    Unknown,   // no such request
    Pending,   // staging or waiting behind earlier requests
    Live,      // entities published; the group is addressable
    Failed,    // staging or import refused; nothing was created
    Despawned, // the group was destroyed on request
};

[[nodiscard]] const char* SceneSpawnStatusName(SceneSpawnStatus status);

class SceneSpawnService
{
public:
    SceneSpawnService(RuntimeWorld& world,
                      const WorldComponentSchema& schema,
                      const ComponentSerializerRegistry& serializers,
                      AsyncTaskQueue& tasks,
                      LoggingProvider& logging);
    ~SceneSpawnService();

    SceneSpawnService(const SceneSpawnService&) = delete;
    SceneSpawnService& operator=(const SceneSpawnService&) = delete;

    // The game's asset front door, handed over once (null on shutdown). The
    // engine borrows it; scene resolution, residency, and handle decode all
    // go through it.
    void ConnectAssets(AssetSystem* assets);

    // Queues a spawn of `sceneAssetPath` (an asset://...smap ref) at `root`,
    // into `partition` -- the persistent partition zero by default, or a
    // zone's partition so the spawn follows that zone's teardown. Always
    // returns a valid id; resolution failures surface through Status.
    [[nodiscard]] SceneSpawnId RequestSpawn(
        std::string_view sceneAssetPath,
        const Transform3f& root,
        StoragePartitionId partition = StoragePartitionId::Default());

    // Queues destruction of a live spawn's entities for the next drain.
    // False when the id is unknown or the spawn is not live.
    bool RequestDespawn(SceneSpawnId id);

    [[nodiscard]] SceneSpawnStatus Status(SceneSpawnId id) const;

    // The spawn's live entities, via the SceneInstanceIndex; empty unless
    // Live (and shrinking as gameplay destroys members).
    [[nodiscard]] std::span<const EntityId> Entities(SceneSpawnId id) const;

    // Owner thread, at the async drain after task completions commit:
    // publishes every consecutive ready request in request order, then
    // executes queued despawns.
    void Pump();

private:
    struct Request;

    void Instantiate(Request& request);
    [[nodiscard]] Request* FindRequest(SceneSpawnId id) const;

    RuntimeWorld& WorldState;
    const WorldComponentSchema& Schema;
    const ComponentSerializerRegistry& Serializers;
    AsyncTaskQueue& Tasks;
    LoggingProvider& Logging;
    AssetSystem* Assets = nullptr;
    std::unique_ptr<SceneSerializationContext> SceneContext;

    // Requests in arrival order; the earliest unpublished one publishes
    // first. Settled requests keep their small record so Status stays
    // answerable for the session's lifetime. Ids mint sequentially and every
    // request appends exactly one record, so Requests[id - 1] IS the record
    // -- the invariant every by-id lookup stands on.
    std::deque<std::unique_ptr<Request>> Requests;
    // The pump scans from here: everything before it is in a terminal state
    // and can never become Ready again, so a long session's spawn history
    // costs the frame nothing.
    std::size_t FirstUnsettled = 0;
    // Despawns queued since the last pump; drained instead of rescanning the
    // whole history for DespawnQueued states.
    std::vector<SceneSpawnId> PendingDespawns;
    std::uint64_t NextSpawnValue = 1;
    std::uint64_t NextInstanceValue = 1;
};
