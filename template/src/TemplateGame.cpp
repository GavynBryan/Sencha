#include "TemplateGame.h"

#include "ObserverFlight.h"

#include "PlayerStartComponent.h"
#include "SpinComponent.h"
#include "TurretMount.h"

#include <abilities/AbilityKit.h>
#include <anim/AnimationClipPlaybackRuntime.h>
#include <anim/AnimationClipPlaybackSystem.h>
#include <audio/AudioSourceRuntime.h>
#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/MovementStatePanel.h>
#endif
#include <app/GameModule.h>
#include <audio/AudioSourceComponent.h>
#include <camera/CameraFollowSystem.h>
#include <camera/CameraRegistration.h>
#include <camera/CameraRig.h>
#include <camera/CameraSeat.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRef.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetStoreTable.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/WorldComponentSchema.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/JumpState.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <movement/MotionComposition.h>
#include <movement/MovementIntent.h>
#include <movement/MovementProfileBindingCache.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputBindingCache.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <net/NetReplicationComponents.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetSpawnPrefab.h>
#include <runtime/spawn/NetPrefabSpawner.h>
#include <world/scene/SceneInstance.h>
#include <net/NetOwnership.h>
#include <net/NetSession.h>
#include <net/PawnCommandCapture.h>
#include <net/PeerCommandRuntime.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantLifecycle.h>
#include <participant/LocalControl.h>
#include <movement/MovementTags.h>
#include <physics/CollisionShapeCache.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/ZoneCollisionLoader.h>
#include <physics/components/CharacterController.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <render/ProbeVolumeSet.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/WorldPartitionIds.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SmapFormat.h>
#include <zone/ZonePackageImporter.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::string_view kAuthoredRoot = "assets";
constexpr std::string_view kCookedScanRoot = "assets/.cooked";
constexpr std::string_view kPlayerAvatarPath =
    "asset://data/player_avatar.sdata";
constexpr std::string_view kInputActionSetPath =
    "asset://data/input_actions.sdata";
constexpr std::string_view kInputProfilePath =
    "asset://data/input_default.sdata";
constexpr std::string_view kGameSettingsPath = "asset://data/game.sdata";
constexpr ZoneId kPlayZone{ 1 };


// A cooked-manifest scene ref ("<.cooked-relative or root-relative path>")
// as the asset:// path the cooked scan root registered it under.
[[nodiscard]] std::string CookedRefToAssetPath(std::string_view ref)
{
    constexpr std::string_view cookedPrefix = ".cooked/";
    if (ref.starts_with(cookedPrefix))
        ref.remove_prefix(cookedPrefix.size());
    return "asset://" + std::string(ref);
}

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

// Where a player goes when the level does not say. Above the origin rather than
// on it, so a body lands on a floor at zero instead of inside it.
inline constexpr Vec3d kDefaultPlayerStart{ 0.0f, 2.0f, 0.0f };

EntityId CreateTransformEntity(
    World& world,
    const Vec3d& position,
    StoragePartitionId partition = PersistentStoragePartition,
    const Vec3d& scale = Vec3d::One())
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

// Wide and low, so the one mesh this template ships reads as a mount rather
// than as a second player standing there. Scale is replicated state, so a
// client's copy arrives with these proportions instead of deriving them.
inline constexpr Vec3d kTurretProportions{ 1.2f, 0.4f, 1.2f };

// A turret, put somewhere because somebody asked for one. Placed rather than
// authored so the template ships the possession path without every level
// having to carry a turret; a real game would author them and this would go.
//
// Authority-only by construction: this is reached from answering a request,
// and a client answers none.
//
// The built-in stand-in for the turret prefab, so it carries by hand the two
// components the prefab authors: where its driver's aim lands, and the opt-in
// that turns the gun to it. Replicated with no prefab to name, so a peer
// receives its state and builds no body for it.
EntityId PlaceTurret(
    World& world,
    const Vec3d& at,
    StoragePartitionId partition)
{
    const EntityId turret =
        CreateTransformEntity(world, at, partition, kTurretProportions);
    world.AddComponent<TurretMount>(turret, TurretMount{});
    world.AddComponent<NetReplicated>(turret);
    world.AddComponent<LookOrientation>(turret, LookOrientation{});
    world.AddComponent<AimFacing>(turret);
    return turret;
}

//=============================================================================
// PlayContentPartition
//
// Which storage partition the loaded play content occupies, published when a
// load finishes. Until one exists there is nowhere to put a pawn.
//
// This is what lets the spawn be a decision taken once content is ready,
// rather than a side effect of whichever load callback happened to run --
// which is what made the answer depend on whether a join beat a map load.
//=============================================================================
struct PlayContentPartition
{
    std::optional<StoragePartitionId> Value;
};

//=============================================================================
// PendingSceneSpawns
//
// Scene spawns settle at the frame drain, but the participant lifecycle asks
// for a body synchronously -- so requests wait here, and the settlement
// system re-asks when one lands. Live prefab bodies are remembered so a
// reaped participant despawns its whole group, not just the root the
// lifecycle knows about.
//=============================================================================
struct PendingSceneSpawns
{
    struct PawnRequest
    {
        EntityId Participant;
        SceneSpawnId Spawn;
    };
    struct TurretRequest
    {
        SceneSpawnId Spawn;
        EntityId Possessor; // invalid = placed without a taker
    };
    std::vector<PawnRequest> Pawns;
    std::vector<TurretRequest> Turrets;
    std::vector<std::pair<EntityId, SceneSpawnId>> LiveBodies;
};

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

void ConfigureRuntimeResources(
    Engine& engine,
    RuntimeAssets& assets)
{
    World& world = engine.World().Entities();

    world.SetResource(assets.Assets.Stores());
    world.SetResource(AudioSourceRuntime{
        &assets.AudioClips, &engine.Audio(), &engine.Captions() });
    world.SetResource(AnimationClipPlaybackRuntime{ &assets.AnimationClips });

    RegisterPhysicsComponents(world);
    RegisterMovement(world);
    RegisterCameraComponents(world);
    RegisterControllerComponents(world);
}

struct WorldPartitionUpdateSystem
{
    explicit WorldPartitionUpdateSystem(
        std::optional<WorldPartitionRuntime>& partition)
        : Partition(partition)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        (void)ctx;
        if (!Partition || !Partition->HasManifest())
            return;

        // Streaming itself is the engine's: it was handed this partition when
        // the world loaded and drives it in the zone-residency phase.
        //
        // What is left here is a gameplay decision. A crossing the destination
        // is not ready for leaves the pawn where the sweep last had it fully
        // inside the room it is leaving; streaming decides that on the wall
        // clock, but moving a pawn is simulation, so the position is recorded
        // and applied on the next fixed tick.
        if (LocalControlSubjectOf(ctx.Entities).IsValid()
            && Partition->LastTraversal().Status
                == DockTraversalStatus::BlockedDestinationNotReady)
        {
            PendingSafePosition = Partition->LastTraversal().SafeSourcePosition;
        }
    }

    // Applied at the head of the tick, before movement runs, so the pawn never
    // enters physics at the position that reached into the unloaded zone.
    void FixedLogic(FixedLogicContext& ctx)
    {
        if (!PendingSafePosition.has_value())
            return;

        World& world = ctx.Entities;
        const EntityId pawn = LocalControlSubjectOf(world);
        if (!pawn.IsValid())
        {
            PendingSafePosition.reset();
            return;
        }

        const Vec3d safe = *PendingSafePosition;
        PendingSafePosition.reset();

        // Through the mover, never onto the transform alone: a character's
        // position lives inside its mover and the transform is where the last
        // sweep left a copy, so writing the copy is undone by the next tick.
        bool moved = false;
        if (Movers != nullptr)
            moved = Movers->SetPosition(world, pawn, safe);
        if (!moved)
        {
            if (LocalTransform* transform = world.TryGet<LocalTransform>(pawn))
                transform->Value.Position = safe;
            RequestTransformHistorySnap(world, pawn);
        }
        if (WorldTransform* transform = world.TryGet<WorldTransform>(pawn))
            transform->Value.Position = safe;
    }

    std::optional<WorldPartitionRuntime>& Partition;
    // Owned by the physics step, which is where characters live. Null in a
    // configuration with no physics, where the transform is all there is.
    CharacterMoverPool* Movers = nullptr;
    // Set by streaming on the wall clock, consumed by the next fixed tick.
    std::optional<Vec3d> PendingSafePosition;
};

#ifdef SENCHA_ENABLE_COOK
// Polls the source watcher on wall time and hands changed files to the
// reloader, which stages an in-place swap that commits at the async drain.
struct HotReloadPollSystem
{
    HotReloadPollSystem(
        std::optional<AssetSourceWatcher>& watcher,
        std::optional<AssetHotReloader>& reloader)
        : Watcher(watcher)
        , Reloader(reloader)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Watcher.has_value() || !Reloader.has_value())
            return;

        Accumulator += ctx.WallDeltaSeconds;
        if (Accumulator < kPollIntervalSeconds)
            return;
        Accumulator = 0.0;

        for (const std::string& changed : Watcher->PollChanged())
            Reloader->ReloadSource(changed);
    }

    static constexpr double kPollIntervalSeconds = 0.3;
    std::optional<AssetSourceWatcher>& Watcher;
    std::optional<AssetHotReloader>& Reloader;
    double Accumulator = 0.0;
};
#endif

struct CharacterInputSystem
{
    void FixedLogic(FixedLogicContext& ctx)
    {
        World& world = ctx.Entities;
        if (!world.IsRegistered<MovementIntent>()
            || !world.IsRegistered<GameplayTagContainer>())
        {
            return;
        }

        const MovementTags* tags =
            world.TryGetResource<MovementTags>();
        if (tags == nullptr)
            return;

        const TemplateInputActions* actionIds =
            world.TryGetResource<TemplateInputActions>();
        if (actionIds == nullptr)
            return;

        // Every controlled entity steers from its own input source: this
        // machine's devices for the player sitting here, a peer's arriving
        // commands for everyone else. Resolving one action state for the whole
        // pass is what would make one player's keys move every pawn at once.
        const InputActionSources sources(world);

        // Each controlled entity steers along its own aim, read from the entity
        // rather than from whatever camera happens to be watching it.
        Query<
            Write<MovementIntent>,
            Read<GameplayTagContainer>,
            Read<LookOrientation>> query(world);
        query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
        {
            auto intents = view.template Write<MovementIntent>();
            const auto entityTags =
                view.template Read<GameplayTagContainer>();
            const auto orientations =
                view.template Read<LookOrientation>();
            for (std::uint32_t index = 0;
                 index < view.Count();
                 ++index)
            {
                if (!entityTags[index].HasExact(tags->Controlled))
                    continue;

                const EntityId steered = view.Entity(index);

                // This tick's actions, not the frame's: a frame that runs
                // several ticks steers each of them, and one that runs none
                // steers nothing.
                const InputActionView input = sources.TickFor(steered);
                const Vec2d move = input.Axis2(actionIds->Move);
                const float strafe = move.X;
                const float forward = move.Y;

                // Whichever moment the action set authored. Jump authors "while
                // held": asking every tick the control is down means a press
                // just before landing fires on the first grounded tick, and
                // holding it hops again on each landing. The gate in the
                // movement step (on the ground, off cooldown) rejects the rest
                // for free.
                const bool jump = input.Fired(actionIds->Jump);

                // A walking body steers on the ground plane whatever it is
                // looking at; a flying one goes where it is looking, which is
                // the difference between the two and the whole of it.
                const bool flying = world.HasComponent<ObserverFlight>(steered);
                const Quatf frame = flying
                    ? Quatf::FromAxisAngle(Vec3d::Up(), orientations[index].Yaw)
                          * Quatf::FromAxisAngle(Vec3d::Right(),
                                                 orientations[index].Pitch)
                    : Quatf::FromAxisAngle(Vec3d::Up(), orientations[index].Yaw);
                Vec3d wish =
                    frame.RotateVector(Vec3d::Forward()) * forward
                    + frame.RotateVector(Vec3d::Right()) * strafe;
                if (!flying)
                    wish.Y = 0.0f;
                const float squared = wish.SqrMagnitude();
                if (squared > 1.0f)
                    wish = wish * (1.0f / std::sqrt(squared));

                intents[index].WishDir = wish;
                intents[index].Jump = jump;

                // Free locomotion projects the wish onto the ground plane, so
                // the vertical part of a flying body's intent has to arrive
                // through the channel that replaces that axis outright --
                // which is also what keeps gravity from being applied to it.
                if (flying)
                {
                    // The same speed the planar channel resolves to, so the
                    // body does not climb faster than it flies forward.
                    const ResolvedMovementTuning* tuning =
                        std::as_const(world).TryGet<ResolvedMovementTuning>(steered);
                    const float speed =
                        tuning != nullptr ? tuning->MaxSpeed
                                          : ResolvedMovementTuning{}.MaxSpeed;
                    (void)ForceSetUpMotionOverride(world, steered,
                                                   wish.Y * speed);
                }
            }
        });
    }
};

// What this game says over the wire. The engine reserves everything below
// kNetFirstGamePayloadKind for itself, so a game's kinds start there and can
// never be swallowed by the dispatch that answers snapshots and commands.
enum : std::uint8_t
{
    // "Put me in that turret", or "take me out of the one I am in". One kind
    // for both because the authority already knows which of them a peer means:
    // it holds the record of who owns what, and a request that disagreed with
    // it would be refused anyway.
    kTurretRequestKind = kNetFirstGamePayloadKind,
};

// The body of a turret request: which turret, by the name replication gave it.
// Hand-written encode and decode against NetWriter/NetReader, which is the
// same idiom every untrusted decode in the engine is written in -- bounded,
// typed failure, nothing sized from what the sender claimed.
std::vector<std::byte> EncodeTurretRequest(NetEntityId target)
{
    std::array<std::byte, sizeof(std::uint64_t)> storage{};
    NetWriter writer(storage);
    writer.WriteU64(target.Value);
    const std::span<const std::byte> written = writer.Written();
    return std::vector<std::byte>(written.begin(), written.end());
}

bool DecodeTurretRequest(std::span<const std::byte> body, NetEntityId& out)
{
    NetReader reader(body);
    std::uint64_t value = 0;
    if (!reader.ReadU64(value) || !reader.AtEnd())
        return false;
    out = NetEntityId{ value };
    return out.IsValid();
}

// The nearest turret to a point, or none. Costs the number of turrets.
EntityId NearestTurret(const World& world, const Vec3d& from)
{
    if (!world.IsRegistered<TurretMount>())
        return EntityId{};

    EntityId nearest;
    float best = 0.0f;
    world.ForEachComponent<TurretMount>(
        [&](EntityId entity, const TurretMount&) {
            const LocalTransform* pose = world.TryGet<LocalTransform>(entity);
            if (pose == nullptr)
                return;
            const float distance = (pose->Value.Position - from).SqrMagnitude();
            if (!nearest.IsValid() || distance < best)
            {
                nearest = entity;
                best = distance;
            }
        });
    return nearest;
}

// Whoever is at this entity's controls, as a participant, or none.
//
// Asked of the participants rather than of ownership because those answer
// different questions: the player at the authority's own machine drives
// without owning anything, so ownership calls their turret free.
EntityId DriverOf(const World& world, EntityId subject)
{
    if (!subject.IsValid() || !world.IsRegistered<ParticipantControl>())
        return EntityId{};

    EntityId found;
    world.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl& held) {
            if (!found.IsValid() && held.ControlSubject == subject)
                found = participant;
        });
    return found;
}

enum class TurretRequestOutcome : std::uint8_t
{
    Took,
    Left,
    Occupied,
    Invalid,
};

//=============================================================================
// Answering a turret request
//
// One function for both directions and for whoever asked, because the question
// is the same one either way: what is this participant driving now, and what
// should it drive next. Everything it decides from is state the authority
// holds -- who drives what, and who owns what -- so a client asking and the
// player at this machine asking are answered by the same rules.
//
// Taking a turret parks the driver's pawn rather than leaving them owning
// both. One participant, one thing driven, which is what the engine's control
// slot says and is a design decision rather than a limitation worked around: a
// player at a fixed gun is not also running around.
//
// `driver` is the peer to hand ownership to, or an invalid peer for the
// authority's own player -- who needs none, because the authority already has
// everything ownership would deliver.
//=============================================================================
TurretRequestOutcome ApplyTurretRequest(
    Engine& engine,
    World& world,
    EntityId participant,
    EntityId turret,
    PeerId driver)
{
    const ParticipantControl* control =
        participant.IsValid()
            ? world.TryGet<ParticipantControl>(participant)
            : nullptr;
    if (control == nullptr)
        return TurretRequestOutcome::Invalid;
    if (!turret.IsValid() || !world.IsAlive(turret)
        || !world.HasComponent<TurretMount>(turret))
    {
        return TurretRequestOutcome::Invalid;
    }

    if (control->ControlSubject == turret)
    {
        // Getting out. The gun goes back to the authority for the next person
        // to ask for, and their input returns to the body that never stopped
        // being theirs. Read before clearing ownership, which is structural
        // and moves the row this points into.
        const EntityId body = control->Body;
        NetClearOwner(world, turret);
        (void)engine.SetParticipantControlSubject(participant, body);
        return TurretRequestOutcome::Left;
    }

    // Somebody else is at the controls. Refused rather than taken: a request
    // that could evict its current driver is a request worth sending
    // constantly.
    if (DriverOf(world, turret).IsValid())
        return TurretRequestOutcome::Occupied;

    // Owned so its owner-only state reaches the driver, and driven so their
    // keys reach it. Two facts, not one: a gun somebody is at the controls of
    // is not necessarily a gun that belongs to them, and it is that difference
    // that leaves the gun standing when its driver quits.
    //
    // Their body keeps its owner through all of this. It is still theirs while
    // they are elsewhere, and only what they drive has moved.
    if (driver.IsValid())
        NetSetOwner(world, turret, driver);
    (void)engine.SetParticipantControlSubject(participant, turret);
    return TurretRequestOutcome::Took;
}

// What a client asked for, decided on the authority. The message carries one
// field -- which turret -- and every other fact is looked up here: who sent it
// comes from the session's own peer record, and what that names comes from the
// identity map replication minted. A message that could be believed about
// either would be a message a peer could use to drive somebody else's body.
bool AnswerTurretRequest(void* context, const NetMessageContext& message)
{
    Engine& engine = *static_cast<Engine*>(context);
    Logger& log = engine.Logging().GetLogger<TemplateGame>();
    World& world = message.Entities;

    NetEntityId named;
    if (!DecodeTurretRequest(message.Body, named))
        return false;
    if (message.Objects == nullptr)
        return false;

    // The one translation a client is allowed to ask for, and it fails closed:
    // an identity this authority never minted, or one it has released, names
    // nothing at all.
    const EntityId turret =
        message.Objects->AuthorityEntities().TryResolve(named);

    // Who is asking, as a participant rather than as a peer number. What they
    // drive and what their body is are both written down there.
    const EntityId participant = NetParticipantForPeer(world, message.From);

    switch (ApplyTurretRequest(engine, world, participant, turret, message.From))
    {
    case TurretRequestOutcome::Took:
        log.Info("TemplateGame: peer {} took the turret", message.From.Value);
        return true;
    case TurretRequestOutcome::Left:
        log.Info("TemplateGame: peer {} left the turret", message.From.Value);
        return true;
    case TurretRequestOutcome::Occupied:
    case TurretRequestOutcome::Invalid:
        break;
    }
    return false;
}

// How far from whoever asked a placed turret stands. Far enough not to be
// inside them, near enough to be the one they get when they ask again.
inline constexpr float kTurretPlacementReach = 3.0f;

// Where the player at this machine is standing, or none when nobody is. A
// dedicated host is the second case and is not an error there: it has no
// player of its own, and everything below still has to happen somewhere.
std::optional<Vec3d> LocalPlayerPosition(const World& world)
{
    const EntityId subject = LocalControlSubjectOf(world);
    if (!subject.IsValid())
        return std::nullopt;
    if (const LocalTransform* here = world.TryGet<LocalTransform>(subject))
        return here->Value.Position;
    return std::nullopt;
}

// Where a turret goes when somebody asks this machine for one: beside whoever
// is playing here, or beside where the level says players begin, so a host
// with nobody at it still puts the gun where people will arrive.
Vec3d TurretPlacementNear(const World& world)
{
    Vec3d at = LocalPlayerPosition(world).value_or(
        FindPlayerStart(world, std::nullopt).value_or(kDefaultPlayerStart));
    at.X += kTurretPlacementReach;
    return at;
}

// Putting one down, wherever this machine would put one. None when there is
// nowhere to put it yet, which is the only way it fails.
//
// Authority-only, structurally: both callers are the authority answering a
// request, and a client answers none.
// A placement either lands now (the procedural turret) or is in flight (the
// settings-named prefab, settling at a later frame's drain).
struct TurretPlacementOutcome
{
    EntityId Turret;
    bool Requested = false;
};

TurretPlacementOutcome PlaceTurretForRequest(
    Engine& engine, World& world, const CompiledGameSettings* settings,
    Logger& log)
{
    const PlayContentPartition* content =
        world.TryGetResource<PlayContentPartition>();
    if (content == nullptr)
        return {};

    if (settings != nullptr && !settings->TurretScenePath.empty())
    {
        Transform3f root = Transform3f::Identity();
        root.Position = TurretPlacementNear(world);
        const SceneSpawnId id = engine.Spawns().RequestSpawn(
            settings->TurretScenePath, root,
            content->Value.value_or(PersistentStoragePartition));
        PendingSpawnsOf(world).Turrets.push_back({ id, EntityId{} });
        log.Info("TemplateGame: placing a turret");
        return { EntityId{}, true };
    }

    const EntityId turret =
        PlaceTurret(world, TurretPlacementNear(world),
                    content->Value.value_or(PersistentStoragePartition));
    log.Info("TemplateGame: placed a turret");
    return { turret, false };
}

// What both console paths say when there is nowhere to put one yet.
constexpr std::string_view kNoContentForTurret =
    "no loaded content to put a turret in";

// The client half of the request: name the turret in terms the authority will
// recognise, and send. Nothing is decided here -- what comes back is a
// snapshot in which somebody drives something.
ConsoleResult AskAuthorityForTurret(Engine& engine, NetSession& session)
{
    ConsoleResult result;
    const World& world = engine.World().Entities();

    if (!session.IsConnected())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("not connected to an authority");
        return result;
    }

    // Already driving one: the same request means getting out, because the
    // authority holds the record of who drives what and can tell the
    // difference without being told.
    EntityId target = LocalControlSubjectOf(world);
    if (!target.IsValid() || !world.HasComponent<TurretMount>(target))
    {
        // Otherwise the nearest one to wherever this player is standing.
        target = NearestTurret(
            world, LocalPlayerPosition(world).value_or(Vec3d::Zero()));
    }

    if (!target.IsValid())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("no turret here");
        return result;
    }

    const NetEntityId named =
        engine.Replication().ClientEntities().TryFind(target);
    if (!named.IsValid())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("that turret is not one replication gave this machine");
        return result;
    }

    // Reliable: a request that is dropped is a key press that did nothing, and
    // nothing later supersedes it. That is a fact about the message rather than
    // something the engine could decide.
    const std::size_t sent = NetSendToAuthority(
        session, NetChannelKind::ReliableOrdered, kTurretRequestKind,
        EncodeTurretRequest(named), &engine.NetTraffic());
    if (sent == 0)
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("could not queue the request");
        return result;
    }

    result.Info("asked the authority about turret "
                + std::to_string(named.Value));
    return result;
}

// Putting one down without getting into it. Separate from taking one because
// they are separate operations with separate callers: a dedicated host places
// the gun its clients will ask for and has nobody to drive it, and conflating
// the two would mean a host could only provide a turret by occupying it.
//
// Authority-only, and that is structural rather than checked twice: this is
// the machine that decides what exists, and a client's turrets arrive
// replicated.
ConsoleResult PlaceTurretHere(
    Engine& engine, const CompiledGameSettings* settings, Logger& log)
{
    ConsoleResult result;
    const TurretPlacementOutcome placed = PlaceTurretForRequest(
        engine, engine.World().Entities(), settings, log);
    if (!placed.Turret.IsValid() && !placed.Requested)
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error(std::string(kNoContentForTurret));
        return result;
    }
    result.Info(placed.Requested ? "placing a turret" : "placed a turret");
    return result;
}

// The same request where this process is the authority the message would have
// gone to, so it answers directly. A standalone game has no session at all; a
// host has one and is still what its own player asks.
//
// Asking for a turret where there is none puts one down, so a player alone
// with a console reaches the possession path in one command. A host wanting a
// gun it does not climb into asks for that instead.
ConsoleResult TakeTurretHere(
    Engine& engine, const CompiledGameSettings* settings, Logger& log)
{
    ConsoleResult result;
    World& world = engine.World().Entities();

    const EntityId participant = LocalParticipantOf(world);
    const ParticipantControl* control =
        participant.IsValid()
            ? world.TryGet<ParticipantControl>(participant)
            : nullptr;
    if (control == nullptr)
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("nobody is playing at this machine");
        return result;
    }
    // Read out now: placing a turret below is structural, and anything after
    // that would be reading a row that has moved.
    EntityId target = control->ControlSubject;

    // Already driving one: the same request means getting out. Otherwise the
    // nearest one to wherever this player is standing, and one put down for
    // them when the level has none.
    if (!target.IsValid() || !world.HasComponent<TurretMount>(target))
    {
        const Vec3d from = LocalPlayerPosition(world).value_or(Vec3d::Zero());
        target = NearestTurret(world, from);
        if (!target.IsValid())
        {
            // A placement already in flight is claimed rather than doubled;
            // otherwise place one, and ride it if the prefab path made that
            // asynchronous.
            PendingSceneSpawns& pending = PendingSpawnsOf(world);
            const auto inFlight = std::find_if(
                pending.Turrets.begin(), pending.Turrets.end(),
                [&](const PendingSceneSpawns::TurretRequest& request)
                {
                    return !request.Possessor.IsValid()
                        || request.Possessor == participant;
                });
            if (inFlight != pending.Turrets.end())
            {
                inFlight->Possessor = participant;
                result.Info("a turret is being placed; taking it when it lands");
                return result;
            }
            const TurretPlacementOutcome placed = PlaceTurretForRequest(
                engine, world, settings, log);
            if (placed.Requested)
            {
                PendingSpawnsOf(world).Turrets.back().Possessor = participant;
                result.Info("a turret is being placed; taking it when it lands");
                return result;
            }
            target = placed.Turret;
        }
        if (!target.IsValid())
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error(std::string(kNoContentForTurret));
            return result;
        }
    }

    switch (ApplyTurretRequest(engine, world, participant, target, PeerId{}))
    {
    case TurretRequestOutcome::Took:
        result.Info("took the turret");
        break;
    case TurretRequestOutcome::Left:
        result.Info("left the turret");
        break;
    case TurretRequestOutcome::Occupied:
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("somebody else is at that turret");
        break;
    case TurretRequestOutcome::Invalid:
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("no turret here");
        break;
    }
    return result;
}

//=============================================================================
// TurretAimSystem
//
// Points a turret where its driver is looking. The engine already hands a
// peer's aim to the entity that peer owns, so this reads what arrived and turns
// it into the one number that travels: no branch on who is driving, no branch
// on whether anybody is.
//=============================================================================
struct TurretAimSystem
{
    void FixedLogic(FixedLogicContext& ctx)
    {
        World& world = ctx.Entities;
        if (!world.IsRegistered<TurretMount>() || !world.IsRegistered<LookOrientation>())
            return;

        Query<Write<TurretMount>, Read<LookOrientation>> query(world);
        query.ForEachChunk([](auto& view) {
            auto mounts = view.template Write<TurretMount>();
            auto looks = view.template Read<LookOrientation>();
            for (std::uint32_t index = 0; index < view.Count(); ++index)
            {
                // Look yaw is a running total and grows without bound; a
                // turret points somewhere, so it is wrapped here rather than
                // sent at a resolution that has to cover every turn a player
                // has ever made.
                constexpr float pi = std::numbers::pi_v<float>;
                float yaw = std::fmod(looks[index].Yaw + pi, 2.0f * pi);
                if (yaw < 0.0f)
                    yaw += 2.0f * pi;
                mounts[index].Yaw = yaw - pi;
            }
        });
    }
};

//=============================================================================
// SessionPlayerSystem
//
// Presents whichever body this machine ended up driving.
//
// Where that body came from is not this system's question any more. Who is a
// participant, which of them this process provides and which arrive replicated,
// and what happens to a body when its player leaves are all decided by the
// engine, at the points where the session role is actually known. What is left
// here is the half that is genuinely a game's: the camera, and giving a pawn
// this machine is about to simulate the rest of its body.
//=============================================================================
struct SessionPlayerSystem
{
    Engine* Owner = nullptr;
    ResolvedPlayerAvatar Avatar;

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        // No role anywhere in here. Who provides participants and who receives
        // them replicated is the engine's decision, taken where the session
        // role is actually known; what is left is presenting whichever body
        // this machine ended up driving.
        DressArrivedBodies(ctx.Entities);
        FollowLocalControl(ctx.Entities);
    }

private:
    Logger& Log() { return Owner->Logging().GetLogger<TemplateGame>(); }

    // Temporary, and the last of its kind: a body that arrived without a mesh
    // gets the avatar's.
    //
    // A pawn a peer receives is instantiated from the prefab the authority
    // named, so everything about it is authored -- except the mesh, which a
    // prefab cannot yet carry because a headless cook has no cache that can
    // hold one. This is what stands in until it can (see
    // docs/plans/pawn-prefab-roadmap.md, P4), and it goes when the avatar data
    // asset does.
    void DressArrivedBodies(World& world)
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
    void FollowLocalControl(World& world)
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
        AttachLocalPlayer(world, subject, Log());
        if (Owner->Prediction().Predicts(subject))
            Log().Info("TemplateGame: predicting this player's own pawn");
    }

    // The pawn this machine was last told to drive, so taking up a new one is
    // an edge rather than something re-derived every frame.
    EntityId Followed;
};

//=============================================================================
// SpawnSettlementSystem
//
// Watches the pending scene spawns each frame, after the drain where the
// spawn service publishes. A settled pawn request re-asks the participant
// lifecycle (ProvideBody consumes the entry either way); a settled turret
// gets its runtime body, its replication stamps, and its waiting driver.
//=============================================================================
struct SpawnSettlementSystem
{
    Engine* Owner = nullptr;
    ResolvedPlayerAvatar Avatar;

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        World& world = ctx.Entities;
        PendingSceneSpawns* pending = world.TryGetResource<PendingSceneSpawns>();
        if (pending == nullptr)
            return;
        SceneSpawnService& spawns = Owner->Spawns();
        Logger& log = Owner->Logging().GetLogger<TemplateGame>();

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

private:
    std::vector<EntityId> ReAskScratch;
};

struct SpinSystem
{
    void FixedLogic(FixedLogicContext& ctx)
    {
        World& world = ctx.Entities;
        if (!world.IsRegistered<SpinComponent>()
            || !world.IsRegistered<LocalTransform>())
        {
            return;
        }

        const float dt =
            static_cast<float>(ctx.Time.DeltaSeconds);
        Query<Write<SpinComponent>, Write<LocalTransform>> query(world);
        query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
        {
            auto spins = view.template Write<SpinComponent>();
            auto transforms = view.template Write<LocalTransform>();
            for (std::uint32_t index = 0;
                 index < view.Count();
                 ++index)
            {
                transforms[index].Value.Rotation =
                    transforms[index].Value.Rotation
                    * Quatf::FromAxisAngle(
                        Vec3d::Up(),
                        spins[index].RadiansPerSecond * dt);
            }
        });
    }
};
} // namespace

void TemplateGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();

    // A dedicated host has no graphics services, so it composes an asset stack
    // that cannot hold a mesh or a texture and loads everything else -- the
    // movement profiles it simulates from, the collision it collides with --
    // through the same front door.
    GraphicsServices* graphics = engine.TryGraphics();
    if (graphics != nullptr)
    {
        Assets.emplace(
            logging,
            graphics->Buffers,
            graphics->Images,
            graphics->Descriptors,
            graphics->Samplers,
            engine.SceneSerializers());
    }
    else
    {
        Assets.emplace(logging, engine.SceneSerializers());
    }
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // This game's own data subtypes, registered into the registries it owns and
    // unregistered in OnShutdown while the module is still mapped: the registry
    // holds function pointers into this module. One list, shared with the data
    // editor through the OnRegisterDataAssetTypes hook.
    OnRegisterDataAssetTypes(runtimeAssets.DataTypes, runtimeAssets.DataSchemas);

    // What a participant is in this game, and where its body comes from. The
    // engine runs the lifecycle -- admit, compose, ask for a body, bind it,
    // reap on departure -- and these answer the two questions only the game
    // can. A peer loop and an orphan sweep used to live here instead.
    engine.Participants().ProvideBody =
        [this](World& world, EntityId participant) -> EntityId
    {
        // Nowhere to put a body until content has loaded. Returning none is an
        // ordinary answer, and the engine does not ask again on its own -- the
        // map load asks, once it has somewhere to put one.
        if (world.TryGetResource<PlayContentPartition>() == nullptr)
            return EntityId{};

        Logger& log = GetEngine().Logging().GetLogger<TemplateGame>();
        const NetParticipantIdentity* who =
            world.TryGet<NetParticipantIdentity>(participant);
        const std::uint32_t peer = who == nullptr ? 0u : who->Peer;

        const auto spawnPosition = [&]() -> Vec3d
        {
            // Unfiltered: a map's content is imported into its own zone
            // partition, so a start looked for only in the persistent one is a
            // start that is never found and a peer that arrives at the origin.
            const std::optional<Vec3d> authored =
                FindPlayerStart(world, std::nullopt);
            // Said out loud once per spawn, because everything downstream of
            // it looks exactly like a level that authored a start at the
            // origin -- including anything else near where a player begins.
            if (!authored.has_value())
            {
                log.Warn("TemplateGame: no player_start in the loaded content; "
                         "spawning at the default position");
            }
            // Offset laterally from the start so two players do not arrive
            // inside each other, by peer id so somebody lands in the same
            // place however many others are present. A proper multi-start
            // rotation is the level's business, not this policy's.
            Vec3d spawn = authored.value_or(kDefaultPlayerStart);
            spawn.X += 2.0f * static_cast<float>(peer);
            return spawn;
        };

        // Named rather than numbered for the one with no peer behind it. Peer
        // zero is the authority, so "a pawn for peer 0" describes the person
        // at this machine as a connection that does not exist.
        const auto announce = [&](std::string_view how)
        {
            if (peer == kNetAuthorityPeer)
                log.Info("TemplateGame: spawned a pawn for the player at "
                         "this machine ({})", how);
            else
                log.Info("TemplateGame: spawned a pawn for peer {} ({})", peer,
                         how);
        };

        // What this game hands a player when the pawn it wanted could not be
        // built. Said at Warn because a player flying a diagnostic body while
        // the game believes it is running is exactly what must not pass
        // unremarked.
        const auto observerPawn = [&]() -> EntityId
        {
            log.Warn("TemplateGame: no player pawn prefab; the player gets the "
                     "built-in observer body, which flies and has no content");
            const EntityId pawn = SpawnObserverPawn(world, spawnPosition());
            announce("observer");
            return pawn;
        };

        const CompiledGameSettings* settings = ResolveGameSettings(log);
        if (settings == nullptr || settings->PlayerPawnScenePath.empty())
            return observerPawn();

        // The prefab path is asynchronous: the first ask requests the spawn
        // and answers "not yet"; the settlement system asks again when the
        // request settles, and this branch then consumes it.
        PendingSceneSpawns& pending = PendingSpawnsOf(world);
        const auto entry = std::find_if(
            pending.Pawns.begin(), pending.Pawns.end(),
            [&](const PendingSceneSpawns::PawnRequest& request)
            { return request.Participant == participant; });
        if (entry == pending.Pawns.end())
        {
            Transform3f root = Transform3f::Identity();
            root.Position = spawnPosition();
            const SceneSpawnId id = GetEngine().Spawns().RequestSpawn(
                settings->PlayerPawnScenePath, root, PersistentStoragePartition);
            pending.Pawns.push_back({ participant, id });
            return EntityId{};
        }

        switch (GetEngine().Spawns().Status(entry->Spawn))
        {
        case SceneSpawnStatus::Pending:
            return EntityId{};
        case SceneSpawnStatus::Live:
        {
            const EntityId root = SpawnedGroupRoot(
                world, GetEngine().Spawns().Entities(entry->Spawn));
            if (!root.IsValid())
            {
                // The group's partition unloaded underneath the request; a
                // fresh ask starts over against the current content.
                pending.Pawns.erase(entry);
                return EntityId{};
            }
            // The prefab is the pawn: its controller, tuning, mode, aim, tags,
            // attributes, and abilities are all authored, and the per-tick
            // columns come with the movement component. Only the mesh is still
            // code's to supply.
            AttachAvatarMesh(world, root, ResolvePlayerAvatar(log));
            StampNetPrefab(world, root, log);
            pending.LiveBodies.emplace_back(participant, entry->Spawn);
            pending.Pawns.erase(entry);
            announce("pawn prefab");
            return root;
        }
        case SceneSpawnStatus::Failed:
        default:
            log.Warn("TemplateGame: pawn prefab '{}' failed to spawn; using "
                     "the built-in pawn", settings->PlayerPawnScenePath);
            pending.Pawns.erase(entry);
            return observerPawn();
        }
    };

    // A prefab body is a group: the engine reaps the root like any body, and
    // the queued despawn sweeps the group's remaining members at the next
    // pump -- without it, prefab children would outlive the pawn outside any
    // group index. Procedural bodies take only the engine-side destroy.
    engine.Participants().ReapBody =
        [this](World& world, EntityId participant, EntityId) -> bool
    {
        PendingSceneSpawns* pending = world.TryGetResource<PendingSceneSpawns>();
        if (pending == nullptr)
            return true;
        const auto live = std::find_if(
            pending->LiveBodies.begin(), pending->LiveBodies.end(),
            [&](const auto& body) { return body.first == participant; });
        if (live == pending->LiveBodies.end())
            return true;
        (void)GetEngine().Spawns().RequestDespawn(live->second);
        pending->LiveBodies.erase(live);
        return true;
    };

    // Where a client's turret request is answered. One kind, one direction, one
    // handler -- and the direction is checked before the handler is reached, so
    // a client sending itself an authority-to-client kind is refused by the
    // router rather than by every handler having to think about it.
    if (!engine.NetMessages().Bind(
            kTurretRequestKind, NetMessageDirection::ClientToAuthority,
            &AnswerTurretRequest,
            &engine))
    {
        engine.Logging().GetLogger<TemplateGame>().Error(
            "TemplateGame: payload kind {} was already answered; turret "
            "requests will not be handled",
            static_cast<unsigned>(kTurretRequestKind));
    }

    ScanAssetsDirectory(
        std::string(kAuthoredRoot),
        runtimeAssets.Registry,
        runtimeAssets.Assets.Kinds());
    ScanAssetsDirectory(
        std::string(kCookedScanRoot),
        runtimeAssets.Registry,
        runtimeAssets.Assets.Kinds());
    RegisterCookedAssets(
        std::string(kAuthoredRoot),
        runtimeAssets.Registry);

    AssetIdMap idMap;
    std::string idMapError;
    const std::string idMapPath =
        std::string(kAuthoredRoot) + "/"
        + std::string(kAssetIdMapFileName);
    if (AssetIdMap::LoadFromFile(
            idMapPath,
            idMap,
            &idMapError))
    {
        ApplyAssetIds(idMap, runtimeAssets.Registry);
    }
    else
    {
        logging.GetLogger<TemplateGame>().Warn(
            "TemplateGame: no asset id map ({}); refs resolve by path only",
            idMapError);
    }

    ConfigureRuntimeResources(engine, runtimeAssets);
    SetupInputMapping(logging.GetLogger<TemplateGame>());
    SceneContext = std::make_unique<SceneSerializationContext>(
        logging,
        &runtimeAssets.Assets);
    ZoneLoader.emplace(
        engine.Tasks(),
        engine.World(),
        engine.RuntimeComponents(),
        engine.SceneSerializers(),
        *SceneContext,
        engine.Runtime());
    Preloader.emplace(
        logging,
        runtimeAssets.Registry,
        runtimeAssets.Assets,
        engine.Tasks());

#ifdef SENCHA_ENABLE_COOK
    HotReloader.emplace(
        logging,
        runtimeAssets.Assets,
        runtimeAssets.Registry,
        HotReloadImporters,
        engine.Tasks(),
        std::string(kAuthoredRoot));
    HotReloadWatcher.emplace(
        logging,
        std::string(kAuthoredRoot),
        std::vector<std::string>{ ".sdata" });
    HotReloadWatcher->Initialize();
#endif

#ifdef SENCHA_ENABLE_DEBUG_UI
    // The other half of the movement tuning loop: the editor predicts what a
    // profile does, this reports what the running game resolved from it.
    // Composed here rather than by the engine overlay because the world being
    // simulated and the data cache holding the profile are both the game's.
    engine.AddDebugPanel(std::make_unique<MovementStatePanel>(
        engine.World().Entities(), &runtimeAssets.DataAssets));
#endif

    // The pipeline object exists headless -- it is registered unconditionally
    // and its extract hook is simply never dispatched -- so the guard that
    // matters is the graphics services its mesh feature is built from.
    if (DefaultRenderPipeline* pipeline = engine.GetRenderPipeline();
        pipeline != nullptr && graphics != nullptr)
    {
        pipeline->SetAssetStores(
            *runtimeAssets.StaticMeshes,
            runtimeAssets.Materials,
            runtimeAssets.MaterialSets,
            runtimeAssets.Textures.get());
        pipeline->AddMeshRenderFeature(*graphics);
    }

    engine.Console().SetMapHandler(
        [this](std::string_view mapName)
        {
            return LoadMap(mapName);
        });

    engine.Console().Registry().RegisterCommand({
        .Name = "world",
        .Owner = "game",
        .Usage = "world <name>",
        .Help = "Load a cooked partitioned world and stream its zones around the player.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string> args)
        {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: world <name>");
                return usage;
            }
            return LoadWorld(args[0]);
        },
    });

    // The spawn service is engine-owned; the asset stack it resolves scenes
    // through is this game's.
    engine.Spawns().ConnectAssets(&runtimeAssets.Assets, &runtimeAssets.Scenes);
    // The same content stack, for the spawns a peer names rather than this
    // machine asking for: without it every replicated prefab is unbuildable
    // and every body a client is sent is deferred forever.
    engine.NetPrefabs().ConnectAssets(&runtimeAssets.Assets, &runtimeAssets.Scenes);

    engine.Console().Registry().RegisterCommand({
        .Name = "scene.spawn",
        .Owner = "game",
        .Usage = "scene.spawn <asset://...smap> [x y z]",
        .Help = "Spawn a cooked scene at the given position (origin by default).",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1 && args.size() != 4)
            {
                result.Error("usage: scene.spawn <asset://...smap> [x y z]");
                return result;
            }
            Transform3f root = Transform3f::Identity();
            if (args.size() == 4)
            {
                try
                {
                    root.Position = Vec3d(std::stof(args[1]), std::stof(args[2]),
                                          std::stof(args[3]));
                }
                catch (const std::exception&)
                {
                    result.Error("scene.spawn: position must be three numbers");
                    return result;
                }
            }
            const SceneSpawnId id =
                GetEngine().Spawns().RequestSpawn(args[0], root);
            result.Info("spawn " + std::to_string(id.Value) + " requested ("
                        + SceneSpawnStatusName(GetEngine().Spawns().Status(id))
                        + ")");
            return result;
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "scene.despawn",
        .Owner = "game",
        .Usage = "scene.despawn <spawn id>",
        .Help = "Destroy a live scene spawn's entities.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
            {
                result.Error("usage: scene.despawn <spawn id>");
                return result;
            }
            SceneSpawnId id{};
            try
            {
                id.Value = std::stoull(args[0]);
            }
            catch (const std::exception&)
            {
                result.Error("scene.despawn: id must be a number");
                return result;
            }
            if (GetEngine().Spawns().RequestDespawn(id))
                result.Info("despawn queued");
            else
                result.Error("spawn " + std::to_string(id.Value) + " is "
                             + SceneSpawnStatusName(
                                 GetEngine().Spawns().Status(id)));
            return result;
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "turret",
        .Owner = "game",
        .Usage = "turret [place]",
        .Help = "Take the nearest turret, or leave the one you are in; "
                "`turret place` puts one down without taking it.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            if (args.size() > 1 || (args.size() == 1 && args[0] != "place"))
            {
                ConsoleResult usage;
                usage.Status = ConsoleStatus::InvalidArguments;
                usage.Error("usage: turret [place]");
                return usage;
            }
            return RequestTurret(!args.empty());
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "camera_mode",
        .Owner = "game",
        .Usage = "camera_mode <first|third|fixed>",
        .Help = "Switch the active camera between first-person, third-person, and the authored pose.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string> args)
        {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: camera_mode <first|third|fixed>");
                return usage;
            }
            return SetCameraMode(args[0]);
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "zone",
        .Owner = "game",
        .Usage = "zone <16-hex zone id>",
        .Help = "Focus the loaded world on a zone.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string> args)
        {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: zone <16-hex zone id>");
                return usage;
            }
            return FocusWorldZone(args[0]);
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "zones",
        .Owner = "game",
        .Usage = "zones",
        .Help = "Print world partition demand and residency.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string>)
        {
            ConsoleResult result;
            if (!Partition || !Partition->HasManifest())
            {
                result.Info("no world loaded (use `world <name>`)");
                return result;
            }

            RuntimeWorld& runtime = GetEngine().World();
            const auto zoneName = [&](ZoneId zone)
            {
                for (const ZoneHeader& header : Partition->Manifest().Zones)
                    if (header.Id == zone)
                        return header.Name;
                return ZoneIdToString(zone);
            };

            result.Info("focus: " + zoneName(Partition->FocusZone()));
            for (const ZoneDemandRecord& record :
                 Partition->DemandRecords())
            {
                const std::string sources = DescribeZoneDemandReasons(record);

                std::string state = "unloaded";
                if (const RuntimeZoneRecord* zone =
                        runtime.FindZone(record.Zone))
                {
                    if (zone->State == RuntimeZoneLoadState::Importing)
                        state = "loading";
                    else
                        state = zone->Participation.Any()
                            ? "live"
                            : "dormant";
                }
                else if (ZoneLoader
                         && ZoneLoader->IsLoading(record.Zone))
                {
                    state = "loading";
                }

                result.Info(
                    "  " + zoneName(record.Zone) + ": "
                    + sources + ", " + state);
            }
            return result;
        },
    });

    // A dedicated host has nobody at a keyboard, so it is told how to serve
    // rather than how to play.
    std::printf("Sencha game template\n");
    std::printf("  Load a map: +map levels/<name>\n");
    std::printf("  Load a world: +world <name>\n");
    if (GetEngine().Config().Runtime.HasLocalPlayer)
        std::printf("  Right mouse: look | WASD: move | Space: jump\n");
    else
        std::printf("  Host a session: +host [port] | see net_status, net_zones\n");
}

// A streamed scene's cooked content, attached while the zone is still hidden:
// collision from the cells the .smap carries, probes from the sibling cooked
// file. The one body both the +map load and every world-zone recipe share.
void TemplateGame::AttachStreamedSceneContent(RuntimeWorld& runtime,
                                              RuntimeZoneRecord& zone,
                                              const SmapContents& contents,
                                              const ProbeVolumeFile& probes)
{
    if (PhysicsShapes != nullptr)
    {
        LoadZoneCollision(
            runtime.Entities(),
            *PhysicsShapes,
            contents.Collision,
            std::string(kCookedScanRoot),
            zone.Partition);
    }
    if (DefaultRenderPipeline* pipeline = GetEngine().GetRenderPipeline())
        AttachZoneProbes(pipeline->GetProbeVolumes(), zone, probes);
}

// The task-thread half beside the scene parse: probe file IO against the
// cooked-scene path convention.
AsyncZoneLoader::SceneStageFn TemplateGame::MakeProbeStage(
    std::string sceneFilePath, std::shared_ptr<ProbeVolumeFile> probes)
{
    return [probes = std::move(probes),
            sceneFilePath = std::move(sceneFilePath)](const SmapContents&)
    {
        (void)ReadZoneProbeFile(sceneFilePath, *probes);
    };
}

ConsoleResult TemplateGame::LoadMap(std::string_view mapName)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    RuntimeAssets& runtimeAssets = RuntimeAssetState();
    ConsoleResult result;

    if (!ZoneLoader)
    {
        result.Error("runtime zone loader is unavailable");
        return result;
    }
    if (Partition && Partition->HasManifest())
    {
        result.Error("a partitioned world is already loaded");
        return result;
    }
    if (engine.World().FindZone(kPlayZone) != nullptr
        || ZoneLoader->IsLoading(kPlayZone))
    {
        result.Error("a map is already loaded or loading");
        return result;
    }

    const std::string sceneAssetPath =
        "asset://" + std::string(mapName) + ".smap";
    const AssetRecord* sceneRecord =
        runtimeAssets.Assets.Resolve(sceneAssetPath, AssetType::Scene);
    if (sceneRecord == nullptr)
    {
        result.Error("no cooked map at '" + sceneAssetPath
                     + "'; cook the level first");
        return result;
    }
    const std::string sceneFilePath = sceneRecord->FilePath;

    // Warm the scene's dependency table before the load; a metadata read that
    // fails leaves the slower resolve-on-import fallback, not an error.
    std::string preloadError;
    std::shared_ptr<AssetPreload> preload =
        Preloader->BeginSceneDependencies(sceneFilePath, &preloadError);
    if (preload == nullptr)
    {
        logging.GetLogger<TemplateGame>().Warn(
            "TemplateGame: no preload for '{}' ({}); resolve-on-import",
            std::string(mapName),
            preloadError);
    }

    auto probes = std::make_shared<ProbeVolumeFile>();
    const AsyncTaskHandle load = ZoneLoader->BeginLoadScene(
        kPlayZone,
        sceneAssetPath,
        runtimeAssets.Assets,
        runtimeAssets.Scenes,
        MakeProbeStage(sceneFilePath, probes),
        [this, probes](
            RuntimeWorld& runtime,
            RuntimeZoneRecord& zone,
            const SmapContents& contents)
        {
            AttachStreamedSceneContent(runtime, zone, contents, *probes);

            // Where a pawn belongs, not a pawn. Who provides one is the
            // session's decision, taken every frame once this exists: a load
            // that finished after a join would otherwise place a second body
            // beside the one the authority is already simulating.
            PublishPlayContent(runtime.Entities(), zone.Partition);
            RequestBodiesForWaitingParticipants(GetEngine());
            PlayZoneActive = true;
            return true;
        },
        ZoneParticipation{
            .Visible = true,
            .Physics = true,
            .Logic = true,
            .Audio = true,
        },
        std::move(preload));
    if (!load.IsValid())
    {
        result.Error("map load refused; see zone load failures");
        return result;
    }

    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

ConsoleResult TemplateGame::LoadWorld(std::string_view worldName)
{
    Engine& engine = GetEngine();
    ConsoleResult result;

    if (PlayZoneActive
        || (ZoneLoader && ZoneLoader->IsLoading(kPlayZone)))
    {
        result.Error("a map is loaded; restart and use +world");
        return result;
    }
    if (Partition && Partition->HasManifest())
    {
        result.Error("a world is already loaded");
        return result;
    }

    const std::string manifestPath =
        std::string(kCookedScanRoot) + "/worlds/"
        + std::string(worldName) + ".sworld.json";
    std::string parseError;
    const std::optional<JsonValue> json =
        JsonParseFile(manifestPath, &parseError);
    if (!json)
    {
        result.Error("world manifest: " + parseError);
        return result;
    }

    std::string manifestError;
    std::optional<WorldPartitionManifest> manifest =
        ReadWorldPartitionManifest(*json, &manifestError);
    if (!manifest)
    {
        result.Error("world manifest rejected: " + manifestError);
        return result;
    }

    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    const EngineRuntimeConfig& runtimeConfig =
        engine.Config().Runtime;
    Partition.emplace(
        [this, assets = &runtimeAssets](const ZoneHeader& header)
        {
            const std::string scenePath =
                std::string(kAuthoredRoot) + "/"
                + header.CookedSceneRef;
            auto probes = std::make_shared<ProbeVolumeFile>();

            ZoneLoadRecipe recipe;
            // Warm the zone's assets (meshes, materials, the lightmap atlas)
            // before attach, from the .smap's own dependency table. A failed
            // metadata read = resolve-on-attach fallback.
            if (Preloader.has_value())
                recipe.Preload = Preloader->BeginSceneDependencies(scenePath);

            ZoneSceneRecipe scene;
            scene.AssetPath = CookedRefToAssetPath(header.CookedSceneRef);
            scene.Assets = &assets->Assets;
            scene.Scenes = &assets->Scenes;
            scene.StageExtra = MakeProbeStage(scenePath, probes);
            scene.Finalize =
                [this, probes](
                    RuntimeWorld& runtime,
                    RuntimeZoneRecord& zone,
                    const SmapContents& contents)
                {
                    AttachStreamedSceneContent(runtime, zone, contents, *probes);
                    return true;
                };
            recipe.Scene = std::move(scene);
            return recipe;
        },
        WorldPartitionStreamingConfig{
            .HopCount = runtimeConfig.StreamingHopCount,
            .LingerSeconds = runtimeConfig.StreamingLingerSeconds,
            .ResidentZoneCap = runtimeConfig.StreamingResidentZoneCap,
            .NeighborVisible = runtimeConfig.StreamingNeighborVisible,
            .NeighborPhysics = runtimeConfig.StreamingNeighborPhysics,
            .Radius = runtimeConfig.StreamingRadius,
        });

    std::string loadError;
    if (!Partition->LoadManifest(
            std::move(*manifest),
            &loadError))
    {
        Partition.reset();
        result.Error("world refused: " + loadError);
        return result;
    }
    const WorldPartitionManifest& loaded = Partition->Manifest();
    if (!loaded.CookedWorldSceneRef.empty())
    {
        // Synchronous through the front door: the world scene loads once at
        // world start, so the async lane buys nothing here, and residency
        // means a later spawn of the same scene shares the parse.
        // The imported entities are the product; the parse is scaffolding
        // that the lease lets go of on every path out of this block.
        const AssetLease worldScene = runtimeAssets.Assets.LoadLease(
            CookedRefToAssetPath(loaded.CookedWorldSceneRef), AssetType::Scene);
        if (!worldScene.IsValid())
        {
            Partition.reset();
            result.Error("world scene '" + loaded.CookedWorldSceneRef
                         + "' failed to load");
            return result;
        }
        const SmapContents* contents = runtimeAssets.Scenes.Get(
            SceneHandle::FromToken(worldScene.OpaqueToken()));

        EntityBuildPackage package;
        SmapError buildError;
        if (!BuildEntityPackageFromSmap(*contents, engine.SceneSerializers(),
                                        package, &buildError))
        {
            Partition.reset();
            result.Error("world scene load error: " + buildError.Message);
            return result;
        }

        ZoneImportError importError;
        if (!ImportPackageIntoPartition(
                engine.World().Entities(),
                engine.RuntimeComponents(),
                package,
                PersistentStoragePartition,
                // The world scene lives in the persistent partition but is
                // saved under the play zone, so its state scope is that zone
                // rather than the partition it occupies.
                kPlayZone,
                engine.SceneSerializers(),
                *SceneContext,
                &importError))
        {
            Partition.reset();
            result.Error(
                "world scene import error: " + importError.Message);
            return result;
        }

        if (PhysicsShapes != nullptr)
        {
            LoadZoneCollision(
                engine.World().Entities(),
                *PhysicsShapes,
                contents->Collision,
                std::string(kCookedScanRoot),
                PersistentStoragePartition);
        }
        else if (!contents->Collision.empty())
        {
            PendingWorldSceneCollision = contents->Collision;
        }
    }

    // A world's scene imports into the persistent partition, so that is where
    // a pawn belongs. Providing one is the session's decision.
    PublishPlayContent(engine.World().Entities(), PersistentStoragePartition);
    RequestBodiesForWaitingParticipants(engine);

    ZoneId focus = PendingZoneFocus;
    PendingZoneFocus = ZoneId{};
    const auto zoneExists = [&](ZoneId zone)
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
            if (header.Id == zone)
                return true;
        return false;
    };
    if (!focus.IsValid() || !zoneExists(focus))
        focus = Partition->Manifest().StartZone;
    if (focus.IsValid() && zoneExists(focus))
        Partition->SetFocus(focus);

    // Handed over once. From here the engine keeps the world loaded around
    // whoever this machine drives and, in a session, around every connected
    // player -- and offers each peer only its own neighbourhood.
    engine.SetWorldStreaming(&*Partition, &*ZoneLoader);

    result.Info("loading world '" + std::string(worldName) + "'");
    return result;
}

ConsoleResult TemplateGame::FocusWorldZone(
    std::string_view zoneHex)
{
    ConsoleResult result;
    const std::optional<ZoneId> zone =
        ZoneIdFromString(zoneHex);
    if (!zone.has_value())
    {
        result.Error(
            "malformed zone id '" + std::string(zoneHex) + "'");
        return result;
    }

    if (Partition && Partition->HasManifest())
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
        {
            if (header.Id == *zone)
            {
                Partition->SetFocus(*zone);
                result.Info(
                    "focus zone " + std::string(zoneHex));
                return result;
            }
        }
        result.Error(
            "zone " + std::string(zoneHex)
            + " is not in the loaded world");
        return result;
    }

    PendingZoneFocus = *zone;
    result.Info("zone focus queued for the next world load");
    return result;
}

// The client's half of taking a turret, and the whole of what a game has to
// write to address a networked object: find the one you mean, ask replication
// what it is called, and send that.
//
// Naming it is the part that could not be written before. A local EntityId is
// an index into one World and means nothing on another machine, so a request
// carrying one would be a request the authority could only guess at; the
// identity map is what turns "this thing in front of me" into something both
// machines agree about, and it refuses to name anything replication did not
// hand this machine -- so a client cannot invent an object and ask for it.
ConsoleResult TemplateGame::RequestTurret(bool placeOnly)
{
    ConsoleResult result;
    Engine& engine = GetEngine();

    if (!engine.World().Entities().IsRegistered<TurretMount>())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("this build has no turrets");
        return result;
    }

    // A client decides nothing about who drives what, or about what exists, so
    // it asks. Anywhere else -- a standalone game, and the player at a host's
    // own machine -- this process is the authority that request would have been
    // sent to, and the same rules answer it without one.
    NetSession* session = engine.TryNet();
    const bool client =
        session != nullptr && session->Role() == NetSessionRole::Client;

    Logger& log = engine.Logging().GetLogger<TemplateGame>();
    if (placeOnly)
    {
        if (client)
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error("only the authority places turrets");
            return result;
        }
        return PlaceTurretHere(engine, ResolveGameSettings(log), log);
    }

    if (client)
        return AskAuthorityForTurret(engine, *session);
    return TakeTurretHere(engine, ResolveGameSettings(log), log);
}

ConsoleResult TemplateGame::SetCameraMode(std::string_view modeName)
{
    ConsoleResult result;

    CameraRigMode mode{};
    if (modeName == "first")
        mode = CameraRigMode::FirstPerson;
    else if (modeName == "third")
        mode = CameraRigMode::ThirdPerson;
    else if (modeName == "fixed")
        mode = CameraRigMode::Fixed;
    else
    {
        result.Error("unknown camera mode '" + std::string(modeName)
                     + "'; expected first, third, or fixed");
        return result;
    }

    World& world = GetEngine().World().Entities();
    const EntityId camera =
        world.GetResource<ActiveCameraService>().GetActive();
    CameraRig* rig = camera.IsValid() ? world.TryGet<CameraRig>(camera) : nullptr;
    if (rig == nullptr)
    {
        result.Error("no active camera with a rig; load a map first");
        return result;
    }

    rig->Mode = mode;
    result.Info("camera mode " + std::string(modeName));
    return result;
}

void TemplateGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    RegisterPhysics(ctx.Schedule);
    if (PhysicsStepSystem* step =
            ctx.Schedule.Get<PhysicsStepSystem>())
    {
        PhysicsShapes = &step->GetShapeCache();
    }

    if (PhysicsShapes != nullptr
        && !PendingWorldSceneCollision.empty())
    {
        LoadZoneCollision(
            GetEngine().World().Entities(),
            *PhysicsShapes,
            PendingWorldSceneCollision,
            std::string(kCookedScanRoot),
            PersistentStoragePartition);
        PendingWorldSceneCollision.clear();
    }

    RegisterAbilityKitSystems(ctx.Schedule);
    // Clip playback advances animation time on the fixed tick; the render
    // extract samples whatever time it leaves behind.
    RegisterAnimationSystems(ctx.Schedule);
    RegisterMovementSystems(ctx.Schedule, RuntimeAssetState().DataAssets,
                            &GetEngine().Logging());
    RegisterInputSystems(
        ctx.Schedule,
        RuntimeAssetState().DataAssets,
        GetEngine().Logging());
    RegisterCameraSystem(ctx.Schedule);
    RegisterControllerSystems(ctx.Schedule);
    RegisterNetSystems(ctx.Schedule, GetEngine().PeerCommands(),
                       GetEngine().Prediction(), GetEngine().Interpolation(),
                       GetEngine().NetClock());
    ctx.Schedule.Register<CharacterInputSystem>();

    // Everything that reads actions runs after they are resolved: the aim
    // integrates on the frame snapshot, the character steers on the tick record
    // along the orientation that produced.
    ctx.Schedule.After<LookIntegrationSystem, InputActionResolveSystem>();
    ctx.Schedule.After<CharacterInputSystem, LookIntegrationSystem>();
    ctx.Schedule.After<CharacterInputSystem, InputActionResolveSystem>();
    // The two edges the net input channel needs around whichever system turns
    // actions into intent. Declared by the engine, which owns why they exist.
    OrderNetInputAround<CharacterInputSystem>(ctx.Schedule);
    OrderMovementAfterInput<CharacterInputSystem>(ctx.Schedule);
    ctx.Schedule.Register<SpinSystem>();
    // A turret points where its driver looks. After the look integrates, for
    // the same reason the character steers after it: the value it reads is
    // this tick's aim rather than last tick's.
    ctx.Schedule.Register<TurretAimSystem>();
    ctx.Schedule.After<TurretAimSystem, LookIntegrationSystem>();

    // Waits on content with no session, and on the authority with one: either
    // way its first act each frame is to ask where this player's pawn comes
    // from.
    {
        Logger& log = GetEngine().Logging().GetLogger<TemplateGame>();
        SessionPlayerSystem& players = ctx.Schedule.Register<SessionPlayerSystem>();
        players.Owner = &GetEngine();
        // Resolves to no body on a process that cannot hold a mesh, which is
        // exactly what a bodyless pawn wants.
        players.Avatar = ResolvePlayerAvatar(log);

        // Settles pending scene spawns before the session presents bodies, so
        // a pawn that lands this frame is followed this frame.
        SpawnSettlementSystem& settlement =
            ctx.Schedule.Register<SpawnSettlementSystem>();
        settlement.Owner = &GetEngine();
        settlement.Avatar = players.Avatar;
        ctx.Schedule.After<SessionPlayerSystem, SpawnSettlementSystem>();
    }

    WorldPartitionUpdateSystem& partitionUpdate =
        ctx.Schedule.Register<WorldPartitionUpdateSystem>(Partition);
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        partitionUpdate.Movers = &step->GetCharacterMovers();
#ifdef SENCHA_ENABLE_COOK
    ctx.Schedule.Register<HotReloadPollSystem>(HotReloadWatcher, HotReloader);
#endif
}

void TemplateGame::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Handled)
        return;

    if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(true);
    }
    else if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
             && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(false);
    }
    else if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        SetRelativeMouseMode(false);
    }
}

void TemplateGame::OnShutdown(GameShutdownContext&)
{
    SetRelativeMouseMode(false);

    Engine& engine = GetEngine();
    RuntimeWorld& runtime = engine.World();

    if (ZoneLoader)
    {
        if (ZoneLoader->IsLoading(kPlayZone))
            (void)ZoneLoader->CancelLoad(kPlayZone);
        if (Partition && Partition->HasManifest())
        {
            for (const ZoneHeader& zone : Partition->Manifest().Zones)
                if (ZoneLoader->IsLoading(zone.Id))
                    (void)ZoneLoader->CancelLoad(zone.Id);
        }
    }

    if (runtime.FindZone(kPlayZone) != nullptr)
        (void)runtime.RequestDetach(kPlayZone);
    if (Partition && Partition->HasManifest())
    {
        for (const ZoneHeader& zone : Partition->Manifest().Zones)
            if (runtime.FindZone(zone.Id) != nullptr)
                (void)runtime.RequestDetach(zone.Id);
    }
    runtime.FlushLifecycleRequests();

    const std::span<const ZoneResidencyChange> changes =
        runtime.BeginResidencyProcessing();
    ZoneResidencyContext residency{
        .Config = engine.Config(),
        .Entities = runtime.Entities(),
        .Changes = changes,
    };
    engine.Schedule().RunZoneResidency(residency);
    runtime.FinalizeResidencyProcessing();

    const std::vector<EntityId> alive =
        runtime.Entities().GetAliveEntities();
    for (EntityId entity : alive)
    {
        if (runtime.Entities().GetEntityPartition(entity)
            == PersistentStoragePartition)
        {
            runtime.Entities().DestroyEntity(entity);
        }
    }

    runtime.Entities()
        .GetResource<ActiveCameraService>()
        .SetActive(EntityId{});
    runtime.Entities().SetResource(AssetStoreTable{});
    runtime.Entities().SetResource(AudioSourceRuntime{});
    runtime.Entities().SetResource(AnimationClipPlaybackRuntime{});

    PlayZoneActive = false;
    // Before the runtime it points at goes.
    GetEngine().SetWorldStreaming(nullptr, nullptr);
    // Same for the spawn services: this game connected them to its asset stack,
    // and the prefab spawner holds a scene reference per resident prefab for the
    // length of the session. Disconnecting drops those while the caches that
    // issued them are still here.
    engine.Spawns().ConnectAssets(nullptr, nullptr);
    engine.NetPrefabs().ConnectAssets(nullptr, nullptr);
    Partition.reset();
    ZoneLoader.reset();
    SceneContext.reset();
    Preloader.reset();
#ifdef SENCHA_ENABLE_COOK
    HotReloadWatcher.reset();
    HotReloader.reset();
#endif

    // The world-resource binding caches hold leases into this game's data-asset
    // cache; every reference must drop before Assets goes away. A lease that
    // outlives its owner calls through a destroyed vtable when the world tears
    // down, which aborts on the way out rather than at the point of the mistake.
    if (MovementProfileBindingCache* bindings =
            runtime.Entities().TryGetResource<MovementProfileBindingCache>())
    {
        bindings->Clear();
    }
    if (InputBindingCache* bindings =
            runtime.Entities().TryGetResource<InputBindingCache>())
    {
        bindings->Clear();
    }
    // Same rule for the context lease. The game object is a module-static whose
    // destructor runs at dlclose, long after the world that owns the context set,
    // so the lease has to be dropped here while its owner still exists.
    GameplayInput.Reset();
    // The spawn recipe is a callable whose target lives in this module, for the
    // same reason the subtype registration below is: it has to go while the
    // module is still mapped.
    // Every lease this game holds into its own data-asset cache, dropped here.
    // Declaration order alone is not enough: Assets is reset explicitly below,
    // so anything still holding a lease at that point outlives its owner and
    // calls through a destroyed vtable when the module unloads.
    InputActionSetAsset.Reset();
    InputProfileAsset.Reset();
    // The pawns that held their own references are destroyed above, so this
    // drops the last one before the caches go away.
    ReleasePlayerAvatar();
    PlayerAvatarAsset.Reset();
    GameSettingsAsset.Reset();
    // The subtype registration holds a function pointer into this module, and
    // unregistering refuses while values are still resident, so it follows the
    // handles above and precedes the cache going away.
    if (Assets.has_value())
        OnUnregisterDataAssetTypes(Assets->DataTypes, Assets->DataSchemas);
    Assets.reset();
}

void TemplateGame::OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                            DataSchemaRegistry& schemas)
{
    RegisterPlayerAvatarData(types, schemas);
    RegisterGameSettingsData(types, schemas);
}

void TemplateGame::OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                              DataSchemaRegistry& schemas)
{
    UnregisterGameSettingsData(types, schemas);
    UnregisterPlayerAvatarData(types, schemas);
}

RuntimeAssets& TemplateGame::RuntimeAssetState()
{
    assert(Assets.has_value()
           && "RuntimeAssets must be constructed before use");
    return *Assets;
}

// Loads one structured data asset synchronously and returns an owned lease.
// Returns an invalid handle on any failure, which every caller treats as
// "run without the authored data" rather than as a fatal error.
DataAssetCacheHandle TemplateGame::AcquireDataAsset(std::string_view path, Logger& log)
{
    RuntimeAssets& assets = RuntimeAssetState();
    AssetLease lease = assets.Assets.LoadLease(path, AssetType::Data);
    if (!lease.IsValid())
    {
        log.Warn("TemplateGame: '{}' did not load; running without it", path);
        return {};
    }

    // The owned handle takes its own reference; the load's goes with the lease
    // at the end of this scope.
    return DataAssetCacheHandle(&assets.DataAssets,
                                DataAssetHandle::FromToken(lease.OpaqueToken()));
}

// Loads the pawn's movement profile synchronously the first time a pawn
// spawns. The asset is game-lifetime, so the owned lease lives on the game;
// the tuning system's binding cache adds its own reference on first resolve.
// Turns the authored avatar paths into mesh and material-set handles, once.
// Every failure path leaves the result invalid, which spawns a bodyless pawn
// rather than refusing to spawn: a missing body is a content problem, not a
// reason to have no player.
const CompiledGameSettings* TemplateGame::ResolveGameSettings(Logger& log)
{
    if (!GameSettingsAsset.IsValid())
        GameSettingsAsset = AcquireDataAsset(kGameSettingsPath, log);
    if (!GameSettingsAsset.IsValid())
        return nullptr;
    const CompiledGameSettings* settings =
        RuntimeAssetState().DataAssets.TryGet<CompiledGameSettings>(
            GameSettingsAsset.GetToken(), "game.settings");
    if (settings == nullptr)
        log.Warn("TemplateGame: '{}' is not a game.settings", kGameSettingsPath);
    return settings;
}

ResolvedPlayerAvatar TemplateGame::ResolvePlayerAvatar(Logger& log)
{
    if (PlayerAvatar.IsValid())
        return PlayerAvatar;

    // A body is something to draw. A process that cannot hold a mesh has no
    // body to give a pawn and is not missing one: the pawn simulates the same
    // either way, and every machine that draws it resolves its own.
    if (!RuntimeAssetState().Assets.HasStore(AssetType::StaticMesh))
        return {};

    if (!PlayerAvatarAsset.IsValid())
        PlayerAvatarAsset = AcquireDataAsset(kPlayerAvatarPath, log);
    if (!PlayerAvatarAsset.IsValid())
        return {};

    RuntimeAssets& assets = RuntimeAssetState();
    const CompiledPlayerAvatar* avatar =
        assets.DataAssets.TryGet<CompiledPlayerAvatar>(
            PlayerAvatarAsset.GetToken(), "player.avatar");
    if (avatar == nullptr)
    {
        log.Warn("TemplateGame: '{}' is not a player.avatar", kPlayerAvatarPath);
        return {};
    }
    AssetLease mesh = assets.Assets.LoadLease(avatar->MeshPath, AssetType::StaticMesh);
    if (!mesh.IsValid())
    {
        log.Warn("TemplateGame: player avatar mesh '{}' did not load",
                 avatar->MeshPath);
        return {};
    }

    // Each material is held only until the set takes its own reference.
    std::vector<AssetLease> materials;
    std::vector<std::uint64_t> materialTokens;
    for (const std::string& path : avatar->MaterialPaths)
    {
        AssetLease material = assets.Assets.LoadLease(path, AssetType::Material);
        if (!material.IsValid())
        {
            log.Warn("TemplateGame: player avatar material '{}' did not load",
                     path);
            return {};
        }
        materialTokens.push_back(material.OpaqueToken());
        materials.push_back(std::move(material));
    }

    AssetLease set = assets.Assets.InternList(AssetType::Material, materialTokens);
    if (!set.IsValid())
    {
        log.Warn("TemplateGame: player avatar materials did not form a set");
        return {};
    }

    PlayerAvatar = ResolvedPlayerAvatar{
        .Mesh = StaticMeshHandle::FromToken(mesh.Relinquish()),
        .Materials = MaterialSetHandle::FromToken(set.Relinquish()),
    };
    return PlayerAvatar;
}

void TemplateGame::ReleasePlayerAvatar()
{
    if (Assets.has_value())
    {
        Assets->Assets.ReleaseLease(AssetType::Material, PlayerAvatar.Materials.ToToken(),
                                    AssetArity::List);
        Assets->Assets.ReleaseLease(AssetType::StaticMesh, PlayerAvatar.Mesh.ToToken());
    }
    PlayerAvatar = {};
}

// Binds the game's controls. The action set loads first: a profile names its
// actions, and binding cannot resolve those names until the set is resident.
void TemplateGame::SetupInputMapping(Logger& log)
{
    RuntimeAssets& assets = RuntimeAssetState();
    World& world = GetEngine().World().Entities();

    InputActionSetAsset = AcquireDataAsset(kInputActionSetPath, log);
    InputProfileAsset = AcquireDataAsset(kInputProfilePath, log);
    if (!InputProfileAsset.IsValid())
    {
        log.Error("TemplateGame: no input profile; the game has no controls");
        return;
    }

    const InputProfileHandle profile{ InputProfileAsset.GetToken() };
    RegisterInputMapping(world, assets.DataAssets, profile);

    // Names resolve to ids once, here. An id outlives a reload of the action
    // set, so every system downstream indexes by id from now on; the resolve
    // system reports whatever failed to bind, including the bindings that were
    // dropped while the rest of the profile bound fine.
    InputBindingCache& bindings = world.GetResource<InputBindingCache>();
    const InputActionRegistry* actions = bindings.GetActions(profile);
    if (actions == nullptr)
    {
        log.Error("TemplateGame: input profile did not bind: {}",
                  DescribeBindErrors(bindings.Status(profile)));
        return;
    }

    TemplateInputActions& ids = world.HasResource<TemplateInputActions>()
        ? world.GetResource<TemplateInputActions>()
        : world.AddResource<TemplateInputActions>();
    ids.Move = actions->Find("move");
    ids.Look = actions->Find("look");
    ids.Jump = actions->Find("jump");

    LookInputBinding& look = world.HasResource<LookInputBinding>()
        ? world.GetResource<LookInputBinding>()
        : world.AddResource<LookInputBinding>();
    look.Look = ids.Look;

    GameplayInput = world.GetResource<InputContextSet>().Activate("gameplay");
}

void TemplateGame::SetRelativeMouseMode(bool enabled)
{
    // No window to capture a pointer into on a headless host.
    PlatformServices* platform = GetEngine().TryPlatform();
    if (platform == nullptr)
        return;

    SdlWindow* window = platform->Windows.GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;
    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) == enabled)
        return;
    SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);
}

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static TemplateGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
