#include "PawnSpawn.h"

#include "PlatformerStart.h"

#include <abilities/AbilityKit.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <attributes/AttributeSet.h>
#include <controller/LookOrientation.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <math/geometry/3d/Transform3d.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementTags.h>
#include <movement/components/CharacterMovement.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <physics/components/CharacterController.h>
#include <world/RuntimeWorld.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <vector>


// Where a level says players begin, or none when it does not say. The two
// answers are kept apart rather than folded into a default here, because a
// level with no start and a level whose start happens to be at the default are
// the same picture from the outside and want different things said about them.
std::optional<Vec3d> FindPlayerStart(
    const World& world,
    std::optional<StoragePartitionId> partition)
{
    if (!world.IsRegistered<PlatformerStart>())
        return std::nullopt;

    for (EntityId entity : world.GetAliveEntities())
    {
        if (partition.has_value()
            && world.GetEntityPartition(entity) != *partition)
        {
            continue;
        }
        if (!world.HasComponent<PlatformerStart>(entity))
            continue;

        if (const LocalTransform* transform =
                world.TryGet<LocalTransform>(entity))
        {
            return transform->Value.Position;
        }
    }
    return std::nullopt;
}

EntityId CreateTransformEntity(
    World& world,
    const Vec3d& position,
    StoragePartitionId partition,
    const Vec3d& scale)
{
    Transform3f transform;
    transform.Position = position;
    transform.Scale = scale;

    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    // WorldTransform is owed by the local one, not written by whoever happens
    // to place an entity; the engine states that obligation in one place.
    SeedDerivedWorldTransform(world, entity);
    return entity;
}

PendingSceneSpawns& PendingSpawnsOf(World& world)
{
    if (PendingSceneSpawns* existing = world.TryGetResource<PendingSceneSpawns>())
        return *existing;
    return world.AddResource<PendingSceneSpawns>();
}

// The spawned group's root: the member without a parent. A prefab meant to be
// spawned as one thing has exactly one; content that ships more is taken by
// its first.
EntityId SpawnedGroupRoot(const World& world, std::span<const EntityId> members)
{
    for (EntityId member : members)
        if (world.TryGet<Parent>(member) == nullptr)
            return member;
    return {};
}

// Content has arrived, so anybody admitted before it can have a body now.
//
// The engine asks once, at admission, and never again on its own -- which is
// what keeps "waiting for a map to load" from being indistinguishable from
// "spectating for good". Asking again is the game's call, and this is the
// moment the answer changes.
void RequestBodiesForWaitingParticipants(Engine& engine)
{
    // This machine's own person, if it has one and is the authority for it.
    // The engine decides both; the game only knows when there is somewhere to
    // put a body, which is now.
    (void)engine.AdmitLocalParticipant();

    World& world = engine.World().Entities();
    if (!world.IsRegistered<ParticipantControl>())
        return;

    std::vector<EntityId> waiting;
    const World& reading = world;
    reading.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl&) {
            waiting.push_back(participant);
        });

    for (const EntityId participant : waiting)
        (void)engine.RequestParticipantBody(participant);
}

void PublishPlayContent(World& world, std::optional<StoragePartitionId> partition)
{
    if (PlayContentPartition* existing = world.TryGetResource<PlayContentPartition>())
        existing->Value = partition;
    else
        world.AddResource<PlayContentPartition>().Value = partition;
}

void SessionPlayerSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    // No role anywhere in here. Who provides participants and who receives
    // them replicated is the engine's decision, taken where the session
    // role is actually known; what is left is presenting whichever body
    // this machine ended up driving.
    FollowLocalControl(ctx.Entities);
}

// What this machine has to do about driving a pawn, once the engine has
// decided which one that is.
//
// This used to be a scan of every entity replication had created, looking
// for one whose NetOwner named this peer -- which is the question the
// engine now answers before the frame's first tick, and answers without an
// unordered_map walk whose winner changed with hash order.
void SessionPlayerSystem::FollowLocalControl(World& world)
{
    const EntityId subject = LocalControlSubjectOf(world);
    if (subject == Followed)
        return;
    Followed = subject;

    if (!subject.IsValid())
        return;

    // Nothing to build. A pawn that arrived replicated was instantiated
    // from the prefab the authority named, so it is already the same
    // archetype the authority is simulating -- which is what makes
    // predicting it from the same input produce the same pawn. The camera
    // that looks through it is PawnCameraSystem's.
    if (Owner->Prediction().Predicts(subject))
        Log->Info("PlatformerGame: predicting this player's own pawn");
}

void SpawnSettlementSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    if (Owner == nullptr)
        return;

    const ZoneId play = Owner->Level().PlayZone();
    if (!play.IsValid())
        return;

    for (const ZoneResidencyChange& change : ctx.Changes)
    {
        if (change.Zone != play)
            continue;

        if (change.Kind == ZoneResidencyChangeKind::Detaching)
        {
            PublishPlayContent(ctx.Entities, std::nullopt);
            continue;
        }
        if (change.Kind != ZoneResidencyChangeKind::Attached)
            continue;

        // Where a pawn belongs, not a pawn. Who provides one is the session's
        // decision, taken every frame once this exists.
        PublishPlayContent(ctx.Entities, change.Partition);
        RequestBodiesForWaitingParticipants(*Owner);
    }
}

void SpawnSettlementSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    PendingSceneSpawns* pending = world.TryGetResource<PendingSceneSpawns>();
    if (pending == nullptr)
        return;
    SceneSpawnService& spawns = Owner->Spawns();

    // Collected first: the re-ask reenters ProvideBody, which edits the
    // very list this walks.
    ReAskScratch.clear();
    for (std::size_t i = 0; i < pending->Pawns.size();)
    {
        const PendingSceneSpawns::PawnRequest& request = pending->Pawns[i];
        if (!world.IsAlive(request.Participant))
        {
            // The participant left before its body landed; the group has
            // nobody to belong to.
            (void)spawns.RequestDespawn(request.Spawn);
            pending->Pawns[i] = pending->Pawns.back();
            pending->Pawns.pop_back();
            continue;
        }
        if (spawns.Status(request.Spawn) != SceneSpawnStatus::Pending)
            ReAskScratch.push_back(request.Participant);
        ++i;
    }
    for (const EntityId participant : ReAskScratch)
        (void)Owner->RequestParticipantBody(participant);
}
