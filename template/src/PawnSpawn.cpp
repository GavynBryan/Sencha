#include "PawnSpawn.h"

#include "ObserverFlight.h"
#include "PlayerStartComponent.h"
#include "TurretMount.h"

#include <abilities/AbilityKit.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <attributes/AttributeSet.h>
#include <camera/CameraRig.h>
#include <camera/CameraSeat.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
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
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <physics/components/CharacterController.h>
#include <render/StaticMeshComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <vector>

#include "TurretControl.h"

EntityId FindFirstCamera(
    const World& world,
    StoragePartitionId partition)
{
    if (!world.IsRegistered<CameraComponent>())
        return EntityId{};

    for (EntityId entity : world.GetAliveEntities())
    {
        if (world.GetEntityPartition(entity) == partition
            && world.TryGet<CameraComponent>(entity) != nullptr)
        {
            return entity;
        }
    }
    return EntityId{};
}

// The camera a body carries for its player to look through, as the body itself
// says: a descendant whose CameraSeat is the primary one.
//
// Not "the first camera child". A pawn may carry several -- a scope, a mirror,
// an angle a cutscene chooses -- and picking by position means adding one
// silently changes which the player looks through, with the symptom appearing
// nowhere near the addition. A second primary is content disagreeing with
// itself, so it is reported rather than resolved.
EntityId PrimaryCameraSeatOf(const World& world, EntityId body, Logger& log)
{
    if (!world.IsRegistered<CameraSeat>() || !world.IsRegistered<Parent>())
        return EntityId{};

    EntityId found;
    for (const EntityId entity : world.GetAliveEntities())
    {
        const Parent* parent = world.TryGet<Parent>(entity);
        if (parent == nullptr || parent->Entity != body)
            continue;
        const CameraSeat* seat = world.TryGet<CameraSeat>(entity);
        if (seat == nullptr || seat->Role != CameraSeatRole::Primary)
            continue;
        if (found.IsValid())
        {
            log.Error("TemplateGame: this body carries more than one primary "
                      "camera seat; using the first and ignoring the rest");
            break;
        }
        found = entity;
    }
    return found;
}

// Where a level says players begin, or none when it does not say. The two
// answers are kept apart rather than folded into a default here, because a
// level with no start and a level whose start happens to be at the default are
// the same picture from the outside and want different things said about them.
std::optional<Vec3d> FindPlayerStart(
    const World& world,
    std::optional<StoragePartitionId> partition)
{
    if (!world.IsRegistered<PlayerStartComponent>())
        return std::nullopt;

    for (EntityId entity : world.GetAliveEntities())
    {
        if (partition.has_value()
            && world.GetEntityPartition(entity) != *partition)
        {
            continue;
        }
        if (!world.HasComponent<PlayerStartComponent>(entity))
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

// Names the prefab a replicated body came from, so a peer instantiates the
// same one instead of being handed loose components to reassemble. Read off the
// group identity the spawn already stamped: the prefab's own asset id.
//
// A body built in code has none, and cannot get one -- there is no asset to
// name. It still replicates its state; it simply arrives on a peer with no
// body, which is the honest consequence of a game whose content did not load.
void StampNetPrefab(World& world, EntityId root, Logger& log)
{
    const SceneInstance* group = world.TryGet<SceneInstance>(root);
    if (group == nullptr || !group->Source.IsValid())
    {
        log.Warn("TemplateGame: a replicated body has no prefab identity; peers "
                 "will see its state and no body");
        return;
    }
    if (!world.HasComponent<NetSpawnPrefab>(root))
    {
        world.AddComponent<NetSpawnPrefab>(root,
                                           NetSpawnPrefab{ .Scene = group->Source });
    }
}

// The body other viewers see. A first-person camera targeting this pawn
// excludes it, so the local player does not sit inside their own mesh; a
// third-person camera draws it. Without a resolved avatar the pawn simply has
// no body, which is a missing asset rather than a broken player.
//
// The one thing a pawn spawned from its prefab still needs from code: a scene
// naming a mesh cannot round-trip through a cook composition that has no mesh
// cache, so the avatar stays a data asset until the mesh moves into the prefab
// (docs/plans/pawn-prefab-roadmap.md, P4).
void AttachAvatarMesh(World& world,
                      EntityId entity,
                      const ResolvedPlayerAvatar& avatar)
{
    if (!avatar.IsValid() || world.HasComponent<StaticMeshComponent>(entity))
        return;
    world.AddComponent<StaticMeshComponent>(
        entity,
        StaticMeshComponent{ .Mesh = avatar.Mesh, .Materials = avatar.Materials });
}

// The body a game gets when the pawn it wanted could not be built: a capsule
// that collides and flies.
//
// Nothing here is content. It exists precisely when content did not load, so
// anything it depended on would be the thing that already failed -- no profile,
// no prefab, no mesh. What it is made of is the movement layer everything else
// uses, steered from the full aim basis rather than the ground plane.
//
// Loud where it is used, not here: a player flying a diagnostic body while the
// game believes it is running is exactly the situation that must not go
// unremarked.
EntityId SpawnObserverPawn(World& world, const Vec3d& at)
{
    const EntityId pawn = CreateTransformEntity(world, at);
    world.AddComponent<CharacterController>(pawn, CharacterController{});
    world.AddComponent<ObserverFlight>(pawn);

    // Brings every per-tick column the movement step reads, so the observer is
    // steered by the same systems a pawn is.
    CharacterMovement movement;
    if (const LocomotionModeRegistry* modes =
            world.TryGetResource<LocomotionModeRegistry>())
    {
        movement.Mode = modes->FreeMode();
    }
    world.AddComponent<CharacterMovement>(pawn, movement);

    // It aims, and it turns to its aim: an observer that could not see where it
    // was going would be no use as a way to look at a level.
    world.AddComponent<LookOrientation>(pawn, LookOrientation{});
    world.AddComponent<AimFacing>(pawn);

    // What the steering pass selects on, and the speed it flies at. Without a
    // profile the tuning resolves to engine defaults plus this attribute, which
    // is the whole answer for a body with no authored feel.
    GameplayTagContainer tags{};
    if (const MovementTags* movementTags = world.TryGetResource<MovementTags>())
        tags.Grant(movementTags->Controlled);
    world.AddComponent<GameplayTagContainer>(pawn, tags);

    AttributeSet attributes{};
    if (const MovementDefs* defs = world.TryGetResource<MovementDefs>())
        attributes.Add(defs->MoveSpeed, 8.0f);
    world.AddComponent<AttributeSet>(pawn, attributes);

    world.AddComponent<AbilitySet>(pawn, AbilitySet{});
    return pawn;
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

// Takes local control of a pawn and points this process's camera at it.
//
// Which entity this machine drives, the look control that follows from it, and
// the prediction that follows from that are the engine's -- one call, so the
// half that used to be forgotten cannot be. What is left here is the camera,
// which is a presentation choice: first person, orbit, or spectator is not a
// fact about the network.
// Points this machine's camera at whatever it is driving.
//
// Only the camera. Which entity that is, whose input reaches it, and whether it
// is predicted are all the engine's answers now -- this reacts to them rather
// than deciding any of them, which is what stops the game from holding a second
// copy of an answer that can go stale.
void AttachLocalPlayer(World& world, EntityId pawn, Logger& log)
{
    const Vec3d position =
        world.TryGet<LocalTransform>(pawn) != nullptr
            ? world.TryGet<LocalTransform>(pawn)->Value.Position
            : Vec3d{};

    // The body's own seat first: a pawn prefab places the camera it is watched
    // from and says how. Falling back to any camera in the world, and then to
    // making one, is what keeps a body with no seat -- the observer, a level
    // whose prefab predates this -- playable rather than blind.
    CameraSeat seat{};
    EntityId camera = PrimaryCameraSeatOf(world, pawn, log);
    if (camera.IsValid())
    {
        seat = *world.TryGet<CameraSeat>(camera);
    }
    else
    {
        camera = FindFirstCamera(world, PersistentStoragePartition);
        if (!camera.IsValid())
        {
            camera = CreateTransformEntity(world, position);
            world.AddComponent<CameraComponent>(camera, CameraComponent{});
        }
    }
    world.GetResource<ActiveCameraService>().SetActive(camera);

    // The rig is provisioned at possession because who is watching is a fact
    // about this machine; what it reads out of the seat is the authored half.
    CameraRig rig{};
    rig.Target = pawn;
    rig.Mode = seat.Mode;
    rig.Distance = seat.Distance;
    if (CameraRig* existing = world.TryGet<CameraRig>(camera))
        *existing = rig;
    else
        world.AddComponent<CameraRig>(camera, rig);

    log.Info("TemplateGame: local player attached to its pawn");
}

void SessionPlayerSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    // No role anywhere in here. Who provides participants and who receives
    // them replicated is the engine's decision, taken where the session
    // role is actually known; what is left is presenting whichever body
    // this machine ended up driving.
    DressArrivedBodies(ctx.Entities);
    FollowLocalControl(ctx.Entities);
}

// Temporary, and the last of its kind: a body that arrived without a mesh
// gets the avatar's.
//
// A pawn a peer receives is instantiated from the prefab the authority
// named, so everything about it is authored -- except the mesh, which a
// prefab cannot yet carry because a headless cook has no cache that can
// hold one. This is what stands in until it can (see
// docs/plans/pawn-prefab-roadmap.md, P4), and it goes when the avatar data
// asset does.
void SessionPlayerSystem::DressArrivedBodies(World& world)
{
    if (!Avatar.IsValid())
        return;

    std::vector<EntityId> undressed;
    Query<Read<CharacterMovement>, Without<StaticMeshComponent>> bodies(world);
    bodies.ForEachChunk([&](auto& view) {
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            undressed.push_back(view.Entity(i));
    });
    for (const EntityId body : undressed)
        AttachAvatarMesh(world, body, Avatar);
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
    // predicting it from the same input produce the same pawn.
    AttachLocalPlayer(world, subject, *Log);
    if (Owner->Prediction().Predicts(subject))
        Log->Info("TemplateGame: predicting this player's own pawn");
}

void SpawnSettlementSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    PendingSceneSpawns* pending = world.TryGetResource<PendingSceneSpawns>();
    if (pending == nullptr)
        return;
    SceneSpawnService& spawns = Owner->Spawns();
    Logger& log = *Log;

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

    for (std::size_t i = 0; i < pending->Turrets.size();)
    {
        const SceneSpawnId id = pending->Turrets[i].Spawn;
        if (spawns.Status(id) == SceneSpawnStatus::Pending)
        {
            ++i;
            continue;
        }
        const EntityId possessor = pending->Turrets[i].Possessor;
        pending->Turrets[i] = pending->Turrets.back();
        pending->Turrets.pop_back();

        if (spawns.Status(id) != SceneSpawnStatus::Live)
        {
            log.Warn("TemplateGame: the turret prefab failed to spawn");
            continue;
        }
        const EntityId root = SpawnedGroupRoot(world, spawns.Entities(id));
        if (!root.IsValid() || world.TryGet<TurretMount>(root) == nullptr)
        {
            // Authoring rule: the mount rides the prefab's root, where
            // possession and NearestTurret address it.
            log.Error("TemplateGame: the turret prefab's root carries no "
                      "turret_mount; dropping the placement");
            (void)spawns.RequestDespawn(id);
            continue;
        }
        world.AddComponent<NetReplicated>(root);
        StampNetPrefab(world, root, log);
        log.Info("TemplateGame: placed a turret");
        if (possessor.IsValid() && world.IsAlive(possessor))
            (void)ApplyTurretRequest(*Owner, world, possessor, root,
                                     PeerId{});
    }
}
