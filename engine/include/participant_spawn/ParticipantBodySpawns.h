#pragma once

#include <participant/ParticipantLifecycle.h>
#include <runtime/spawn/SceneSpawnService.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Logger;
class World;

struct ParticipantBodySpawnRequest
{
    std::string ScenePath;
    Transform3f Root = Transform3f::Identity();
    StoragePartitionId Partition = StoragePartitionId::Default();
};

// A participant's body request held as one root-owned scene spawn until it is
// assigned or ended. Game-owned over borrowed services: Close before they or the
// callbacks' captures die. World owner thread, outside queries and hooks.
class ParticipantBodySpawns
{
public:
    // Selected once per attempt. None stores nothing and schedules no retry.
    using SelectSpawn = std::function<std::optional<ParticipantBodySpawnRequest>(
        const World&, EntityId participant)>;
    // Runs once on the landed root before assignment. May add components or
    // assign the root itself; a root held as a body is never despawned here.
    using PrepareBody = std::function<void(World&, EntityId participant, EntityId body)>;
    // Bind to Engine::AssignParticipantBody to keep session projection.
    using AssignBody = std::function<ParticipantBodyChange(
        EntityId participant, EntityId body)>;

    ParticipantBodySpawns(World& world, SceneSpawnService& spawns, Logger& log,
                          SelectSpawn select, AssignBody assign,
                          PrepareBody prepare = {});
    ~ParticipantBodySpawns();
    ParticipantBodySpawns(const ParticipantBodySpawns&) = delete;
    ParticipantBodySpawns& operator=(const ParticipantBodySpawns&) = delete;
    ParticipantBodySpawns(ParticipantBodySpawns&&) = delete;
    ParticipantBodySpawns& operator=(ParticipantBodySpawns&&) = delete;

    // For a participant with no living body and no attempt in flight.
    void StartSpawn(EntityId participant);
    void Update();

    // A published group still awaiting assignment is destroyed at the next pump.
    bool CancelPending(EntityId participant);
    void CancelAllPending();
    void Close();

private:
    struct PendingSpawn
    {
        SceneSpawnId Spawn;
        std::string Path;
    };
    struct Attempt
    {
        EntityId Participant;
        SceneSpawnId Spawn;
    };

    bool NeedsBody(EntityId participant) const;
    bool MatchesPending(EntityId participant, SceneSpawnId spawn) const;
    void Settle(const Attempt& attempt);

    World& Entities;
    SceneSpawnService& Spawns;
    Logger& Log;
    SelectSpawn Select;
    AssignBody Assign;
    PrepareBody Prepare;
    std::unordered_map<EntityId, PendingSpawn, EntityIdHash> Pending;
    std::vector<Attempt> Settling;
    bool Updating = false;
    bool Closed = false;
};
