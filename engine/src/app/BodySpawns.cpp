#include <app/BodySpawns.h>

#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cassert>
#include <utility>

namespace
{
    struct CallScope
    {
        explicit CallScope(bool& active) : Active(active)
        {
            assert(!Active && "recursive body spawn callback");
            Active = true;
        }
        ~CallScope() { Active = false; }
        bool& Active;
    };
}

BodySpawns::BodySpawns(
    World& world, SceneSpawnService& spawns, Logger& log,
    SelectSpawn select, RequestBody request, PrepareBody prepare)
    : Entities(world), Spawns(spawns), Log(log), Select(std::move(select)),
      Request(std::move(request)), Prepare(std::move(prepare))
{
    assert(Select && Request);
}

BodySpawns::~BodySpawns()
{
    assert(Closed && "BodySpawns requires Close before destruction");
}

bool BodySpawns::NeedsBody(EntityId participant) const
{
    const auto* control = std::as_const(Entities).TryGet<ParticipantControl>(participant);
    return control != nullptr && !Entities.IsAlive(control->Body);
}

bool BodySpawns::MatchesPending(EntityId participant, SceneSpawnId spawn) const
{
    const auto found = Pending.find(participant);
    return found != Pending.end() && found->second.Spawn == spawn;
}

EntityId BodySpawns::ProvideBody(EntityId participant)
{
    if (Closed)
        return {};
    assert(!Entities.InQueryScope());
    CallScope call(Providing);
    if (!NeedsBody(participant))
    {
        (void)CancelPending(participant);
        return {};
    }

    const auto found = Pending.find(participant);
    if (found == Pending.end())
    {
        const auto selected = Select(std::as_const(Entities), participant);
        if (selected && NeedsBody(participant))
        {
            const SceneSpawnId spawn = Spawns.RequestSpawn(
                selected->ScenePath, selected->Root, selected->Partition);
            Pending.emplace(participant, PendingSpawn{ spawn, selected->ScenePath });
        }
        return {};
    }

    const SceneSpawnId spawn = found->second.Spawn;
    if (Spawns.IsDespawnRequested(spawn))
    {
        (void)CancelPending(participant);
        return {};
    }
    const SceneSpawnStatus status = Spawns.Status(spawn);
    if (status == SceneSpawnStatus::Pending)
        return {};
    if (status != SceneSpawnStatus::Live)
    {
        Log.Error("Participant prefab '{}' for {}:{} ended as {}",
                  found->second.Path, participant.Index, participant.Generation,
                  SceneSpawnStatusName(status));
        (void)CancelPending(participant);
        return {};
    }

    EntityId root;
    std::size_t roots = 0;
    for (EntityId member : Spawns.Entities(spawn))
    {
        if (Entities.IsAlive(member)
            && std::as_const(Entities).TryGet<Parent>(member) == nullptr)
        {
            root = member;
            ++roots;
        }
    }
    if (roots != 1)
    {
        Log.Error("Participant prefab '{}' has {} live roots; expected one",
                  found->second.Path, roots);
        (void)CancelPending(participant);
        return {};
    }

    if (Prepare)
        Prepare(Entities, participant, root);

    // Preparation can change archetypes, destroy IDs, or cancel the request.
    // Neither the earlier map iterator nor any component pointer is reused.
    if (!MatchesPending(participant, spawn))
        return {};
    if (!NeedsBody(participant) || !Entities.IsAlive(root)
        || Spawns.IsDespawnRequested(spawn)
        || Spawns.Status(spawn) != SceneSpawnStatus::Live)
    {
        (void)CancelPending(participant);
        return {};
    }

    Bodies.emplace(root, spawn);
    Pending.erase(participant);
    return root;
}

bool BodySpawns::CancelPending(EntityId participant)
{
    const auto found = Pending.find(participant);
    if (found == Pending.end())
        return false;
    (void)Spawns.RequestDespawn(found->second.Spawn);
    Pending.erase(found);
    return true;
}

void BodySpawns::CancelAllPending()
{
    // A game may cancel from a completion callback. Keep the update's action
    // snapshot intact, and preserve request order for the cancellation batch.
    std::vector<SceneSpawnId> spawns;
    spawns.reserve(Pending.size());
    for (const auto& [participant, pending] : Pending)
        spawns.push_back(pending.Spawn);
    std::ranges::sort(spawns);
    for (SceneSpawnId spawn : spawns)
        (void)Spawns.RequestDespawn(spawn);
    Pending.clear();
}

bool BodySpawns::RequestDespawnBody(EntityId body)
{
    const auto found = Bodies.find(body);
    if (found == Bodies.end())
        return false;
    (void)Spawns.RequestDespawn(found->second);
    Bodies.erase(found);
    return true;
}

void BodySpawns::CollectDeadBodies()
{
    for (const auto& [body, spawn] : Bodies)
        if (!Entities.IsAlive(body) || Spawns.IsDespawnRequested(spawn))
            Actions.push_back({ body, spawn, true });
}

void BodySpawns::SortActions()
{
    std::ranges::sort(Actions, {}, &Action::Spawn);
}

void BodySpawns::Update()
{
    if (Closed)
        return;
    assert(!Entities.InQueryScope() && !Providing);
    CallScope call(Updating);
    Actions.clear();
    for (const auto& [participant, pending] : Pending)
        if (!NeedsBody(participant) || Spawns.IsDespawnRequested(pending.Spawn)
            || Spawns.Status(pending.Spawn) != SceneSpawnStatus::Pending)
            Actions.push_back({ participant, pending.Spawn });
    CollectDeadBodies();
    SortActions();

    for (const Action action : Actions)
    {
        if (action.IsBody)
        {
            const auto found = Bodies.find(action.Entity);
            if (found != Bodies.end() && found->second == action.Spawn)
                (void)RequestDespawnBody(action.Entity);
            continue;
        }
        if (!MatchesPending(action.Entity, action.Spawn))
            continue;
        if (!NeedsBody(action.Entity) || Spawns.IsDespawnRequested(action.Spawn))
        {
            (void)CancelPending(action.Entity);
            continue;
        }
        if (Spawns.Status(action.Spawn) == SceneSpawnStatus::Live)
            (void)Request(action.Entity);
        else
            (void)ProvideBody(action.Entity); // consume terminal failure, never retry

        // A replaced policy or another living body may ignore the completion.
        // Only end this attempt; callbacks may have already started a new one.
        if (MatchesPending(action.Entity, action.Spawn))
            (void)CancelPending(action.Entity);
    }

    // Assignment can reject and destroy a freshly returned root. Observe that
    // after all callbacks, so its children are queued for the next pump too.
    Actions.clear();
    CollectDeadBodies();
    SortActions();
    for (const Action action : Actions)
        (void)RequestDespawnBody(action.Entity);
}

void BodySpawns::Close()
{
    if (Closed)
        return;
    assert(!Updating && !Providing && !Entities.InQueryScope());
    Actions.clear();
    for (const auto& [participant, pending] : Pending)
        Actions.push_back({ participant, pending.Spawn });
    for (const auto& [body, spawn] : Bodies)
        Actions.push_back({ body, spawn, true });
    SortActions();
    for (const Action action : Actions)
        (void)Spawns.RequestDespawn(action.Spawn);
    Pending.clear();
    Bodies.clear();
    Actions.clear();
    Select = {};
    Request = {};
    Prepare = {};
    Closed = true;
}
