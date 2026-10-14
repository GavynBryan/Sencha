#include <participant_spawn/ParticipantBodySpawns.h>

#include <core/logging/Logger.h>
#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <utility>

ParticipantBodySpawns::ParticipantBodySpawns(
    World& world, SceneSpawnService& spawns, Logger& log,
    SelectSpawn select, AssignBody assign, PrepareBody prepare)
    : Entities(world), Spawns(spawns), Log(log), Select(std::move(select)),
      Assign(std::move(assign)), Prepare(std::move(prepare))
{
    assert(Select && Assign);
}

ParticipantBodySpawns::~ParticipantBodySpawns()
{
    assert(Closed && "ParticipantBodySpawns requires Close before destruction");
}

bool ParticipantBodySpawns::NeedsBody(EntityId participant) const
{
    const auto* control = std::as_const(Entities).TryGet<ParticipantControl>(participant);
    return control != nullptr && !Entities.IsAlive(control->Body);
}

bool ParticipantBodySpawns::MatchesPending(EntityId participant, SceneSpawnId spawn) const
{
    const auto found = Pending.find(participant);
    return found != Pending.end() && found->second.Spawn == spawn;
}

void ParticipantBodySpawns::StartSpawn(EntityId participant)
{
    if (Closed)
        return;
    assert(!Entities.InQueryScope());
    if (!NeedsBody(participant) || Pending.contains(participant))
        return;

    const auto selected = Select(std::as_const(Entities), participant);
    if (!selected)
        return;
    const SceneSpawnId spawn = Spawns.RequestSpawn(
        selected->ScenePath, selected->Root, selected->Partition,
        SceneSpawnLifetime::OwnedByRoot);
    Pending.emplace(participant, PendingSpawn{ spawn, selected->ScenePath });
}

void ParticipantBodySpawns::Update()
{
    if (Closed)
        return;
    assert(!Entities.InQueryScope() && !Updating);
    Updating = true;

    Settling.clear();
    for (const auto& [participant, pending] : Pending)
        if (!NeedsBody(participant) || Spawns.IsDespawnRequested(pending.Spawn)
            || Spawns.Status(pending.Spawn) != SceneSpawnStatus::Pending)
            Settling.push_back({ participant, pending.Spawn });
    std::ranges::sort(Settling, {}, &Attempt::Spawn);

    // Callbacks may cancel or start attempts, so every step looks up again.
    for (const Attempt attempt : Settling)
        if (MatchesPending(attempt.Participant, attempt.Spawn))
            Settle(attempt);

    Updating = false;
}

void ParticipantBodySpawns::Settle(const Attempt& attempt)
{
    const auto [participant, spawn] = attempt;
    if (Spawns.IsDespawnRequested(spawn))
    {
        Pending.erase(participant);
        return;
    }

    const SceneSpawnStatus status = Spawns.Status(spawn);
    if (status == SceneSpawnStatus::Pending)
    {
        (void)CancelPending(participant);
        return;
    }
    if (status != SceneSpawnStatus::Live)
    {
        Log.Error("Participant prefab '{}' for {}:{} ended as {}",
                  Pending.at(participant).Path, participant.Index,
                  participant.Generation, SceneSpawnStatusName(status));
        Pending.erase(participant);
        return;
    }

    const EntityId root = Spawns.Root(spawn);
    if (root.IsValid() && NeedsBody(participant) && Prepare)
    {
        Prepare(Entities, participant, root);
        if (!MatchesPending(participant, spawn))
            return;
    }

    Pending.erase(participant);
    if (root.IsValid() && NeedsBody(participant) && !Spawns.IsDespawnRequested(spawn)
        && Assign(participant, root).Status == ParticipantBodyStatus::Assigned)
        return;

    // Whoever holds the root as a body holds its group; otherwise it is nobody's.
    if (!Entities.IsAlive(root) || !ParticipantWithBody(Entities, root).IsValid())
        (void)Spawns.RequestDespawn(spawn);
}

bool ParticipantBodySpawns::CancelPending(EntityId participant)
{
    const auto found = Pending.find(participant);
    if (found == Pending.end())
        return false;
    (void)Spawns.RequestDespawn(found->second.Spawn);
    Pending.erase(found);
    return true;
}

void ParticipantBodySpawns::CancelAllPending()
{
    std::vector<SceneSpawnId> spawns;
    spawns.reserve(Pending.size());
    for (const auto& [participant, pending] : Pending)
        spawns.push_back(pending.Spawn);
    std::ranges::sort(spawns);
    for (const SceneSpawnId spawn : spawns)
        (void)Spawns.RequestDespawn(spawn);
    Pending.clear();
}

void ParticipantBodySpawns::Close()
{
    if (Closed)
        return;
    assert(!Updating && !Entities.InQueryScope());
    CancelAllPending();
    Settling.clear();
    Select = {};
    Assign = {};
    Prepare = {};
    Closed = true;
}
