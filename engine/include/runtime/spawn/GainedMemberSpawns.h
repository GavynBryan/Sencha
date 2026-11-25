#pragma once

#include <core/identity/Id.h>
#include <ecs/StoragePartitionId.h>
#include <math/geometry/3d/Transform3d.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <zone/ZoneStateScope.h>

#include <vector>

class LoggingProvider;
class RuntimeWorld;

// Makes each gained member a zone's import wrote from its source (GainedSpawnRequests) through the spawn
// service: docs/gameplay/saves.md, "What a zone gained". Owner thread, at the async drain.
class GainedMemberSpawns
{
public:
    GainedMemberSpawns(RuntimeWorld& world, SceneSpawnService& spawns, LoggingProvider& logging);

    // Before the pump: spawns what an attached zone waits for and withdraws what nothing waits for any more.
    void Request();
    // After the pump: a published root takes its place and is kept with its zone; a failed one is forgotten
    // with what it held.
    void Settle();

private:
    struct Spawning
    {
        ZoneStateScope Scope;
        StoragePartitionId Partition;
        PersistentEntityId Id;
        Transform3f Root = Transform3f::Identity();
        SceneSpawnId Spawn;
    };

    void Forget(const Spawning& spawning, const char* why);

    RuntimeWorld& WorldState;
    SceneSpawnService& Spawns;
    LoggingProvider& Logging;
    std::vector<Spawning> InFlight;
};
