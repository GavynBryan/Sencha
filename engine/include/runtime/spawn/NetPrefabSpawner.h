#pragma once

#include <core/assets/AssetId.h>
#include <core/assets/AssetLease.h>
#include <net/NetSpawnPrefab.h>
#include <world/scene/SceneInstance.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class AssetSystem;
class ComponentSerializerRegistry;
class LoggingProvider;
class RuntimeWorld;
class SceneCache;
class WorldComponentSchema;
struct SceneSerializationContext;

//=============================================================================
// NetPrefabSpawner
//
// The receiving half of a replicated spawn: an AssetId arrives, and the entity
// it names is built here.
//
// Synchronous, unlike SceneSpawnService beside it, and for a reason rather than
// convenience -- the spawn is being applied inside a snapshot, and there is
// nowhere to put a half-arrived entity while a load finishes. So the first
// snapshot naming a prefab makes it resident and defers, and the next one
// builds it. A prefab is a small cooked scene and this happens once per prefab
// per session; what would not be acceptable is doing it per spawn, which is
// why residency is held rather than re-established.
//
// The package is built per spawn even though the scene is held once: each group
// carries its own instance identity, which is what makes the children of one
// pawn distinguishable from the children of another when either is torn down.
//=============================================================================
class NetPrefabSpawner final : public INetPrefabSpawner
{
public:
    NetPrefabSpawner(RuntimeWorld& world,
                     const WorldComponentSchema& schema,
                     const ComponentSerializerRegistry& serializers,
                     LoggingProvider& logging);
    ~NetPrefabSpawner() override;

    NetPrefabSpawner(const NetPrefabSpawner&) = delete;
    NetPrefabSpawner& operator=(const NetPrefabSpawner&) = delete;

    // Null disconnects, which is what a host with no content stack is. Every
    // prefab then reads as unavailable rather than as an entity with no body.
    // `scenes` is the cache the Scene kind commits into, read for contents.
    //
    // A resolved prefab is held resident for the session, so a game that
    // connects its own asset stack has to disconnect before that stack goes
    // away -- these references belong to caches this does not own.
    void ConnectAssets(AssetSystem* assets, SceneCache* scenes);

    [[nodiscard]] NetPrefabReadiness Prepare(AssetId scene) override;
    [[nodiscard]] EntityId Instantiate(AssetId scene,
                                       World& world,
                                       StoragePartitionId partition) override;
    void Despawn(World& world, EntityId root) override;

    // Diagnostics: how many distinct prefabs this session has resolved, and how
    // many it refused. A client whose refusals climb is running content its
    // authority does not have.
    [[nodiscard]] std::size_t ResolvedCount() const { return Resident.size(); }
    [[nodiscard]] std::size_t RefusedCount() const { return Refused.size(); }

private:
    struct ResidentPrefab
    {
        std::string Path;
        AssetLease Scene;
    };

    // Logged once per id: a peer sending the same unresolvable spawn every
    // snapshot would otherwise fill the log with one problem.
    void RefuseOnce(AssetId scene, std::string_view reason);

    RuntimeWorld& WorldState;
    const WorldComponentSchema& Schema;
    const ComponentSerializerRegistry& Serializers;
    LoggingProvider& Logging;
    AssetSystem* Assets = nullptr;
    SceneCache* Scenes = nullptr;
    std::unique_ptr<SceneSerializationContext> SceneContext;

    std::unordered_map<AssetId, ResidentPrefab> Resident;
    std::unordered_set<AssetId> Refused;
    std::uint32_t NextInstanceValue = 1;
};
