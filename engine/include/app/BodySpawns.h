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

struct BodySpawnRequest
{
    std::string ScenePath;
    Transform3f Root = Transform3f::Identity();
    StoragePartitionId Partition = StoragePartitionId::Default();
};

// The scene spawns that are or will be bodies: requests waiting on
// participants, and groups handed off as bodies. Pending requests follow
// participants; handed-off groups follow their roots, including roots
// preserved by a game's reap veto. SceneSpawnService remains the owner of
// publication, membership, and queued destruction.
//
// Game-owned, with borrowed services. Call Close before those services or game
// callback captures die. A closed object may safely outlive the engine.
// Operations run on the world owner thread, outside queries/lifecycle hooks.
class BodySpawns
{
public:
    // Selected once per new attempt. None stores no work and schedules no retry.
    using SelectSpawn = std::function<std::optional<BodySpawnRequest>(
        const World&, EntityId participant)>;
    // Runs once before assignment. May add components; IDs are revalidated after.
    using PrepareBody = std::function<void(World&, EntityId participant, EntityId body)>;
    // Bind to Engine::RequestParticipantBody to preserve session projection.
    using RequestBody = std::function<ParticipantBodyChange(EntityId participant)>;

    BodySpawns(World& world, SceneSpawnService& spawns, Logger& log,
                            SelectSpawn select, RequestBody request,
                            PrepareBody prepare = {});
    ~BodySpawns();
    BodySpawns(const BodySpawns&) = delete;
    BodySpawns& operator=(const BodySpawns&) = delete;
    BodySpawns(BodySpawns&&) = delete;
    BodySpawns& operator=(BodySpawns&&) = delete;

    // Bind explicitly to the game's ProvideBody policy. Requires one live,
    // parentless group member. Loading answers none until an explicit request
    // or Update hands the completion back through the participant operation.
    EntityId ProvideBody(EntityId participant);
    void Update();

    // Also cancel published groups still awaiting handoff. Accepted bodies are
    // unaffected. Destruction, when needed, occurs at the next scene pump.
    bool CancelPending(EntityId participant);
    void CancelAllPending();
    // Recognizes a tracked root even after it dies. A reap veto should skip this.
    bool RequestDespawnBody(EntityId body);
    void Close();

private:
    struct PendingSpawn
    {
        SceneSpawnId Spawn;
        std::string Path;
    };
    struct Action
    {
        EntityId Entity;
        SceneSpawnId Spawn;
        bool IsBody = false;
    };

    bool NeedsBody(EntityId participant) const;
    bool MatchesPending(EntityId participant, SceneSpawnId spawn) const;
    void CollectDeadBodies();
    void SortActions();

    World& Entities;
    SceneSpawnService& Spawns;
    Logger& Log;
    SelectSpawn Select;
    RequestBody Request;
    PrepareBody Prepare;
    std::unordered_map<EntityId, PendingSpawn, EntityIdHash> Pending;
    std::unordered_map<EntityId, SceneSpawnId, EntityIdHash> Bodies;
    std::vector<Action> Actions;
    // Update -> Request -> ProvideBody is intentional reentry. Recursing into
    // either operation, or closing while callbacks execute, is a contract error.
    bool Updating = false;
    bool Providing = false;
    bool Closed = false;
};
