#include "TemplateGame.h"

#include "PlayerStartComponent.h"
#include "SpinComponent.h"

#include <abilities/AbilityKit.h>
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
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/assets/AssetRegistry.h>
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
#include <movement/MovementComponents.h>
#include <movement/MovementIntent.h>
#include <movement/MovementProfileBindingCache.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionState.h>
#include <input/InputBindingCache.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>
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
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneLoadPackage.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZonePackageSceneLoader.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
constexpr std::string_view kAuthoredRoot = "assets";
constexpr std::string_view kCookedScanRoot = "assets/.cooked";
constexpr std::string_view kPlayerMovementProfilePath =
    "asset://data/player_movement.sdata";
constexpr std::string_view kPlayerAvatarPath =
    "asset://data/player_avatar.sdata";
constexpr std::string_view kInputActionSetPath =
    "asset://data/input_actions.sdata";
constexpr std::string_view kInputProfilePath =
    "asset://data/input_default.sdata";
constexpr ZoneId kPlayZone{ 1 };

struct SceneBuildResult
{
    bool Success = false;
    std::string Error;
};

std::optional<JsonValue> ParseSceneFile(
    const std::string& path,
    std::string& error)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        error = "could not open scene file '" + path + "'";
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    std::optional<JsonValue> json =
        JsonParse(buffer.str(), &parseError);
    if (!json)
    {
        error = "scene JSON parse error at "
            + std::to_string(parseError.Position)
            + ": " + parseError.Message;
    }
    return json;
}

void BuildScenePackage(
    ZoneLoadPackage& package,
    SceneBuildResult& result,
    const std::string& scenePath,
    const ComponentSerializerRegistry& serializers)
{
    std::string parseError;
    const std::optional<JsonValue> json =
        ParseSceneFile(scenePath, parseError);
    if (!json)
    {
        result.Error = std::move(parseError);
        return;
    }

    SceneLoadError loadError;
    if (!BuildZonePackageFromSceneJson(
            *json,
            serializers,
            package,
            &loadError))
    {
        result.Error = loadError.Message;
        return;
    }

    result.Success = true;
    result.Error.clear();
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

Vec3d FindPlayerStart(
    const World& world,
    std::optional<StoragePartitionId> partition)
{
    if (!world.IsRegistered<PlayerStartComponent>())
        return Vec3d(0.0f, 2.0f, 0.0f);

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
    return Vec3d(0.0f, 2.0f, 0.0f);
}

EntityId CreateTransformEntity(
    World& world,
    const Vec3d& position,
    StoragePartitionId partition = PersistentStoragePartition)
{
    Transform3f transform;
    transform.Position = position;

    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    world.AddComponent<WorldTransform>(
        entity,
        WorldTransform{ transform });
    return entity;
}

// The pawn itself: everything that makes a body move and be seen, and nothing
// about who is watching it. A remote player's pawn is exactly this and no more,
// which is why the camera and the local input marks are not in here.
EntityId SpawnPawn(
    World& world,
    const Vec3d& spawnPosition,
    MovementProfileHandle movementProfile,
    const ResolvedPlayerAvatar& avatar)
{
    const EntityId pawn =
        CreateTransformEntity(world, spawnPosition);
    world.AddComponent<CharacterController>(
        pawn,
        CharacterController{});
    world.AddComponent<MovementIntent>(
        pawn,
        MovementIntent{});
    world.AddComponent<KinematicState>(pawn, KinematicState{});
    world.AddComponent<SupportState>(pawn, SupportState{});
    world.AddComponent<ResolvedMovementTuning>(pawn, ResolvedMovementTuning{});
    world.AddComponent<LocomotionOutput>(pawn, LocomotionOutput{});
    world.AddComponent<MotionAxisOverride>(pawn, MotionAxisOverride{});
    world.AddComponent<MotionImpulse>(pawn, MotionImpulse{});
    world.AddComponent<MotionRequest>(pawn, MotionRequest{});
    world.AddComponent<ModeTransitionRequest>(pawn, ModeTransitionRequest{});

    // With an invalid profile handle the pawn resolves tuning from defaults
    // plus the MoveSpeed attribute, so a missing asset degrades to movement
    // that still works.
    CharacterMovement pawnMovement;
    pawnMovement.Profile = movementProfile;
    if (const LocomotionModeRegistry* modes =
            world.TryGetResource<LocomotionModeRegistry>())
    {
        pawnMovement.Mode = modes->FreeMode();
    }
    world.AddComponent<CharacterMovement>(pawn, pawnMovement);

    // The pawn moves every tick and is what the camera watches, so it renders
    // interpolated between ticks rather than stepping at the tick rate.
    world.AddComponent<WorldTransformHistory>(pawn, WorldTransformHistory{});

    // The body other viewers see. A first-person camera targeting this pawn
    // excludes it, so the local player does not sit inside their own mesh; a
    // third-person camera draws it. Without a resolved avatar the pawn simply
    // has no body, which is a missing asset rather than a broken player.
    if (avatar.IsValid())
    {
        world.AddComponent<StaticMeshComponent>(
            pawn,
            StaticMeshComponent{
                .Mesh = avatar.Mesh,
                .Materials = avatar.Materials,
            });
    }

    const MovementDefs* movementDefs =
        world.TryGetResource<MovementDefs>();

    GameplayTagContainer pawnTags{};
    if (const MovementTags* movementTags =
            world.TryGetResource<MovementTags>())
    {
        pawnTags.Grant(movementTags->Controlled);
    }
    world.AddComponent<GameplayTagContainer>(pawn, pawnTags);

    // The profile's base layer owns the authored top speed; this attribute is
    // the effect-modifiable base and the whole answer when no profile loads,
    // so keep it at a modest speed rather than a tuned one.
    AttributeSet pawnAttributes{};
    if (movementDefs != nullptr)
        pawnAttributes.Add(movementDefs->MoveSpeed, 4.5f);
    world.AddComponent<AttributeSet>(pawn, pawnAttributes);

    AbilitySet pawnAbilities{};
    if (movementDefs != nullptr)
        pawnAbilities.Grant(movementDefs->Jump);
    world.AddComponent<AbilitySet>(pawn, pawnAbilities);

    // The pawn aims; a camera presents it. Every pawn has an orientation --
    // a remote player is aiming somewhere too, and that is what makes their
    // body face the right way.
    world.AddComponent<LookOrientation>(pawn, {});

    return pawn;
}

// Points this process's camera and local input at a pawn. Exactly one pawn per
// process gets this: the one this player drives. Everything here is a
// presentation or input fact about this machine, which is why none of it
// replicates.
void AttachLocalPlayer(World& world, EntityId pawn, Logger& log)
{
    const Vec3d position =
        world.TryGet<LocalTransform>(pawn) != nullptr
            ? world.TryGet<LocalTransform>(pawn)->Value.Position
            : Vec3d{};

    EntityId camera = FindFirstCamera(world, PersistentStoragePartition);
    if (!camera.IsValid())
    {
        camera = CreateTransformEntity(world, position);
        world.AddComponent<CameraComponent>(camera, CameraComponent{});
    }
    world.GetResource<ActiveCameraService>().SetActive(camera);

    // The tag marks this as the entity the local player's look action turns.
    if (!world.HasComponent<LocalLookControl>(pawn))
        world.AddComponent<LocalLookControl>(pawn, {});

    CameraRig rig{};
    rig.Target = pawn;
    rig.Mode = CameraRigMode::FirstPerson;
    if (CameraRig* existing = world.TryGet<CameraRig>(camera))
        *existing = rig;
    else
        world.AddComponent<CameraRig>(camera, rig);

    log.Info("TemplateGame: local player attached to its pawn");
}

EntityId SpawnPlayerAvatar(
    World& world,
    Logger& log,
    std::optional<StoragePartitionId> spawnPartition,
    MovementProfileHandle movementProfile,
    const ResolvedPlayerAvatar& avatar)
{
    const EntityId pawn = SpawnPawn(
        world, FindPlayerStart(world, spawnPartition), movementProfile, avatar);
    AttachLocalPlayer(world, pawn, log);
    return pawn;
}

void ConfigureRuntimeResources(
    Engine& engine,
    RuntimeAssets& assets)
{
    World& world = engine.World().Entities();

    if (StaticMeshComponentAssets* meshAssets =
            world.TryGetResource<StaticMeshComponentAssets>())
    {
        meshAssets->Meshes = &assets.StaticMeshes;
        meshAssets->MaterialSets = &assets.MaterialSets;
    }
    else
    {
        world.AddResource<StaticMeshComponentAssets>(
            &assets.StaticMeshes,
            &assets.MaterialSets);
    }

    if (ZoneLightmapComponentAssets* lightmapAssets =
            world.TryGetResource<ZoneLightmapComponentAssets>())
    {
        lightmapAssets->Textures = &assets.Textures;
    }
    else
    {
        world.AddResource<ZoneLightmapComponentAssets>(&assets.Textures);
    }

    if (AudioSourceRuntime* audioRuntime =
            world.TryGetResource<AudioSourceRuntime>())
    {
        audioRuntime->Clips = &assets.AudioClips;
        audioRuntime->Audio = &engine.Audio();
        audioRuntime->Captions = &engine.Captions();
    }
    else
    {
        world.AddResource<AudioSourceRuntime>(
            &assets.AudioClips,
            &engine.Audio(),
            &engine.Captions());
    }

    RegisterPhysicsComponents(world);
    RegisterMovement(world);
    RegisterCameraComponents(world);
    RegisterControllerComponents(world);
}

struct WorldPartitionUpdateSystem
{
    WorldPartitionUpdateSystem(
        std::optional<WorldPartitionRuntime>& partition,
        std::optional<AsyncZoneLoader>& loader,
        RuntimeWorld& runtimeWorld,
        EntityId& pawn)
        : Partition(partition)
        , Loader(loader)
        , Runtime(runtimeWorld)
        , Pawn(pawn)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Partition || !Partition->HasManifest() || !Loader)
            return;

        World& world = ctx.Entities;
        if (Pawn.IsValid())
        {
            if (const WorldTransform* transform =
                    world.TryGet<WorldTransform>(Pawn))
            {
                Partition->SetFocus(transform->Value.Position);
            }
            if (const CharacterController* controller =
                    world.TryGet<CharacterController>(Pawn))
            {
                Partition->SetFocusCapsule(
                    controller->Radius,
                    controller->Height);
            }
        }
        Partition->Update(
            ctx.WallDeltaSeconds,
            *Loader,
            Runtime);

        // A crossing the destination is not ready for leaves the pawn where the
        // sweep last had it fully inside the source zone. Streaming decides
        // that here, on the wall clock, but moving the pawn is simulation, so
        // the position is recorded and applied on the next fixed tick.
        if (Pawn.IsValid()
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
        if (!PendingSafePosition.has_value() || !Pawn.IsValid())
            return;

        World& world = ctx.Entities;
        const Vec3d safe = *PendingSafePosition;
        PendingSafePosition.reset();

        bool moved = false;
        if (world.HasResource<CharacterMoverPool>())
        {
            moved = world.GetResource<CharacterMoverPool>().SetPosition(
                world, Pawn, safe);
        }
        if (!moved)
        {
            if (LocalTransform* transform = world.TryGet<LocalTransform>(Pawn))
                transform->Value.Position = safe;
            RequestTransformHistorySnap(world, Pawn);
        }
        if (WorldTransform* transform = world.TryGet<WorldTransform>(Pawn))
            transform->Value.Position = safe;
    }

    std::optional<WorldPartitionRuntime>& Partition;
    std::optional<AsyncZoneLoader>& Loader;
    RuntimeWorld& Runtime;
    EntityId& Pawn;
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
        const MovementDefs* defs =
            world.TryGetResource<MovementDefs>();
        AbilityActivationQueue* activations =
            world.TryGetResource<AbilityActivationQueue>();
        if (tags == nullptr || defs == nullptr)
            return;

        const TemplateInputActions* actionIds =
            world.TryGetResource<TemplateInputActions>();
        const InputActionState* actions =
            world.TryGetResource<InputActionState>();
        if (actionIds == nullptr || actions == nullptr)
            return;

        // This tick's resolved actions, not the frame's: a frame that runs
        // several ticks steers each of them, and one that runs none steers
        // nothing.
        const InputActionView input = actions->Tick();
        const Vec2d move = input.Axis2(actionIds->Move);
        const float strafe = move.X;
        const float forward = move.Y;

        // Whichever moment the action set authored. Jump authors "while held":
        // queueing the ability every tick while the control is down means a
        // press just before landing fires on the first grounded tick, and
        // holding it hops again on each landing. The activation gate (grounded,
        // cooldown) rejects the rest for free.
        const bool jump = input.Fired(actionIds->Jump);

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

                const Quatf frame = Quatf::FromAxisAngle(
                    Vec3d::Up(), orientations[index].Yaw);
                Vec3d wish =
                    frame.RotateVector(Vec3d::Forward()) * forward
                    + frame.RotateVector(Vec3d::Right()) * strafe;
                wish.Y = 0.0f;
                const float squared = wish.SqrMagnitude();
                if (squared > 1.0f)
                    wish = wish * (1.0f / std::sqrt(squared));

                intents[index].WishDir = wish;
                if (jump && activations != nullptr)
                {
                    activations->Pending.push_back(
                        { view.Entity(index), defs->Jump });
                }
            }
        });
    }
};

//=============================================================================
// SessionPlayerSystem
//
// Keeps the set of player pawns in step with the set of peers, from whichever
// side of the session this process is on.
//
// On the authority: every connected peer gets a pawn, marked replicated and
// owned by them, and loses it when they go. On a client: the pawns arrive as
// replicated state, and this gives them the body every machine already has the
// content for and points the camera at the one this player owns.
//
// Deliberately not two systems behind a role check inside one: the two halves
// share no state and the role is fixed for a session, so the branch is a
// dispatch on a value that cannot change rather than a growing behavior hub.
//=============================================================================
struct SessionPlayerSystem
{
    Engine* Owner = nullptr;
    MovementProfileHandle Profile;
    ResolvedPlayerAvatar Avatar;
    // The pawn this process's player drives, so a client attaches once rather
    // than every frame.
    EntityId LocalPawn;
    std::unordered_map<std::uint32_t, EntityId> PeerPawns;

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        World& world = ctx.Entities;
        NetSession* session = Owner == nullptr ? nullptr : Owner->TryNet();
        if (session == nullptr)
            return;
        if (!world.IsRegistered<NetReplicated>() || !world.IsRegistered<NetOwner>())
            return;

        if (session->Role() == NetSessionRole::Host)
            ServePeers(world, *session);
        else if (session->Role() == NetSessionRole::Client && session->IsConnected())
            AdoptReplicatedPawns(world, session->LocalPeerId().Value);
    }

private:
    Logger& Log() { return Owner->Logging().GetLogger<TemplateGame>(); }

    // The one pawn this machine's player drives, marked by the tag the look
    // input follows. There is at most one per process by construction.
    static EntityId FindLocallyControlledPawn(const World& world)
    {
        if (!world.IsRegistered<LocalLookControl>())
            return EntityId{};
        for (EntityId entity : world.GetAliveEntities())
        {
            if (world.HasComponent<LocalLookControl>(entity))
                return entity;
        }
        return EntityId{};
    }

    void ServePeers(World& world, NetSession& session)
    {
        // The host is a player too. Without this a client would see everyone
        // except the person running the server, which is the one pawn they are
        // most likely to be standing next to. Found by its local-control mark
        // rather than remembered from startup, because the pawn is spawned when
        // a map loads and that can be after this system is registered.
        const EntityId local = FindLocallyControlledPawn(world);
        if (local.IsValid() && !world.HasComponent<NetReplicated>(local))
            world.AddComponent<NetReplicated>(local);

        const std::vector<PeerId> peers = session.ConnectedPeers();

        for (PeerId peer : peers)
        {
            if (PeerPawns.contains(peer.Value))
                continue;

            // Offset laterally from the authored start so two players do not
            // arrive inside each other. A proper multi-start rotation is the
            // level's business, not this system's.
            Vec3d spawn = FindPlayerStart(world, PersistentStoragePartition);
            spawn.X += 2.0f * static_cast<float>(PeerPawns.size() + 1);

            const EntityId pawn = SpawnPawn(world, spawn, Profile, Avatar);
            world.AddComponent<NetReplicated>(pawn);
            world.AddComponent<NetOwner>(pawn, NetOwner{ .Peer = peer.Value });
            PeerPawns.emplace(peer.Value, pawn);
            Log().Info("TemplateGame: spawned a pawn for peer {}", peer.Value);
        }

        // A peer that left takes its pawn with it.
        for (auto it = PeerPawns.begin(); it != PeerPawns.end(); )
        {
            const bool present = std::any_of(
                peers.begin(), peers.end(),
                [id = it->first](PeerId peer) { return peer.Value == id; });
            if (present)
            {
                ++it;
                continue;
            }
            if (world.IsAlive(it->second))
                world.DestroyEntity(it->second);
            Log().Info("TemplateGame: removed the pawn for peer {}", it->first);
            it = PeerPawns.erase(it);
        }
    }

    void AdoptReplicatedPawns(World& world, std::uint32_t self)
    {
        // Everything replication created for this client, which is the only
        // definitive list: an entity is not marked on this side, and querying
        // by NetOwner would miss every pawn the authority drives itself --
        // including the host's own player.
        std::vector<EntityId> needBody;
        EntityId mine;
        for (const auto& [id, entity] : Owner->Replication().ClientEntities().All())
        {
            if (!world.IsAlive(entity))
                continue;
            if (!world.HasComponent<StaticMeshComponent>(entity))
                needBody.push_back(entity);
            if (const NetOwner* owner = world.TryGet<NetOwner>(entity);
                owner != nullptr && owner->Peer == self)
            {
                mine = entity;
            }
        }

        // The wire carries values, never content. Both machines already have
        // the avatar, so a client resolves it locally rather than being told
        // what to load by whoever it connected to.
        if (Avatar.IsValid())
        {
            for (EntityId entity : needBody)
            {
                world.AddComponent<StaticMeshComponent>(
                    entity,
                    StaticMeshComponent{ .Mesh = Avatar.Mesh,
                                         .Materials = Avatar.Materials });
                // Replicated pawns move every tick, so they present
                // interpolated between ticks like the local one does.
                world.AddComponent<WorldTransformHistory>(
                    entity, WorldTransformHistory{});
                Log().Info("TemplateGame: gave a body to a replicated pawn");
            }
        }

        if (mine.IsValid() && mine != LocalPawn)
        {
            // The pawn this process spawned for itself before joining is now
            // someone else's job to simulate. Leaving it would leave a second
            // body standing where the player used to be.
            const EntityId previous = FindLocallyControlledPawn(world);
            if (previous.IsValid() && previous != mine)
            {
                world.RemoveComponent<LocalLookControl>(previous);
                world.DestroyEntity(previous);
            }

            AttachLocalPlayer(world, mine, Log());
            LocalPawn = mine;
        }
    }
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

void TemplateGame::OnRegisterComponents(
    ComponentSerializerRegistry& serializers)
{
    // The engine already registered its own scene manifest into this registry;
    // a game adds only what it owns.
    RegisterComponent<SpinComponent>(serializers);
    RegisterComponent<PlayerStartComponent>(serializers);
}

void TemplateGame::OnUnregisterComponents(
    ComponentSerializerRegistry& serializers)
{
    serializers.Remove(ResolveComponentTypeId<SpinComponent>());
    serializers.Remove(ResolveComponentTypeId<PlayerStartComponent>());
}

void TemplateGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(
        logging,
        graphics.Buffers,
        graphics.Images,
        graphics.Descriptors,
        graphics.Samplers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // This game's own data subtypes, registered into the registries it owns and
    // unregistered in OnShutdown while the module is still mapped: the registry
    // holds function pointers into this module.
    RegisterPlayerAvatarData(runtimeAssets.DataTypes, runtimeAssets.DataSchemas);

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

    if (DefaultRenderPipeline* pipeline =
            engine.GetRenderPipeline())
    {
        pipeline->SetAssetStores(
            runtimeAssets.StaticMeshes,
            runtimeAssets.Materials,
            runtimeAssets.MaterialSets,
            &runtimeAssets.Textures);
        pipeline->AddMeshRenderFeature(graphics);
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

    std::printf("Sencha game template\n");
    std::printf("  Load a map: +map levels/<name>\n");
    std::printf("  Load a world: +world <name>\n");
    std::printf("  Right mouse: look | WASD: move | Space: jump\n");
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

    const std::string base =
        std::string(kCookedScanRoot) + "/"
        + std::string(mapName);
    const std::string scenePath = base + ".cooked.json";
    const std::string manifestPath = base + ".manifest.json";
    const std::string collisionSidecar =
        base + ".collision.json";

    std::shared_ptr<AssetPreload> preload;
    AssetManifest manifest;
    std::string manifestError;
    if (LoadAssetManifestFile(
            manifestPath,
            manifest,
            &manifestError))
    {
        preload = Preloader->Begin(
            ResolveManifestPaths(
                manifest,
                runtimeAssets.Registry));
    }
    else
    {
        logging.GetLogger<TemplateGame>().Warn(
            "TemplateGame: no manifest for '{}' ({}); resolve-on-import",
            std::string(mapName),
            manifestError);
    }

    auto buildResult = std::make_shared<SceneBuildResult>();
    const ComponentSerializerRegistry* serializers = &engine.SceneSerializers();
    auto probes = std::make_shared<ProbeVolumeFile>();
    ZoneLoader->BeginLoad(
        kPlayZone,
        [buildResult, probes, serializers, scenePath](
            ZoneLoadPackage& package)
        {
            BuildScenePackage(
                package,
                *buildResult,
                scenePath,
                *serializers);
            (void)ReadZoneProbeFile(scenePath, *probes);
        },
        [this, buildResult, probes, collisionSidecar, &logging](
            RuntimeWorld& runtime,
            RuntimeZoneRecord& zone)
        {
            if (!buildResult->Success)
            {
                logging.GetLogger<TemplateGame>().Error(
                    "TemplateGame: scene load error: {}",
                    buildResult->Error);
                return false;
            }

            if (PhysicsShapes != nullptr)
            {
                LoadZoneCollision(
                    runtime.Entities(),
                    *PhysicsShapes,
                    collisionSidecar,
                    std::string(kCookedScanRoot),
                    zone.Partition);
            }

            if (DefaultRenderPipeline* pipeline =
                    GetEngine().GetRenderPipeline())
            {
                AttachZoneProbes(
                    pipeline->GetProbeVolumes(), zone, *probes);
            }

            if (!PlayerPawn.IsValid())
            {
                Logger& log = logging.GetLogger<TemplateGame>();
                PlayerPawn = SpawnPlayerAvatar(
                    runtime.Entities(),
                    log,
                    zone.Partition,
                    ResolvePlayerMovementProfile(log),
                    ResolvePlayerAvatar(log));
            }
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

    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

ConsoleResult TemplateGame::LoadWorld(std::string_view worldName)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
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
    std::ifstream file(manifestPath);
    if (!file.is_open())
    {
        result.Error(
            "no cooked world manifest at '" + manifestPath + "'");
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    JsonParseError parseError;
    const std::optional<JsonValue> json =
        JsonParse(buffer.str(), &parseError);
    if (!json)
    {
        result.Error(
            "world manifest parse error at "
            + std::to_string(parseError.Position)
            + ": " + parseError.Message);
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
    AssetSystem* assetSystem = &runtimeAssets.Assets;
    LoggingProvider* loggingPtr = &logging;
    const ComponentSerializerRegistry* serializers = &engine.SceneSerializers();

    const EngineRuntimeConfig& runtimeConfig =
        engine.Config().Runtime;
    Partition.emplace(
        [this, assetSystem, loggingPtr, serializers](
            const ZoneHeader& header)
        {
            const std::string scenePath =
                std::string(kAuthoredRoot) + "/"
                + header.CookedSceneRef;
            const std::string collisionPath =
                std::string(kAuthoredRoot) + "/"
                + header.CookedCollisionRef;
            auto buildResult =
                std::make_shared<SceneBuildResult>();
            auto probes = std::make_shared<ProbeVolumeFile>();

            ZoneLoadRecipe recipe;
            // Warm the zone's assets (meshes, materials, the lightmap atlas)
            // before attach, the same manifest convention as the map path:
            // the manifest sits beside the cooked scene (.cooked.json ->
            // .manifest.json). Missing manifest = resolve-on-attach fallback.
            {
                std::string assetManifestPath = scenePath;
                constexpr std::string_view cookedSuffix = ".cooked.json";
                if (assetManifestPath.ends_with(cookedSuffix))
                {
                    assetManifestPath.resize(
                        assetManifestPath.size() - cookedSuffix.size());
                    assetManifestPath += ".manifest.json";
                    AssetManifest assetManifest;
                    if (Preloader.has_value()
                        && LoadAssetManifestFile(assetManifestPath, assetManifest, nullptr))
                    {
                        recipe.Preload = Preloader->Begin(ResolveManifestPaths(
                            assetManifest, RuntimeAssetState().Registry));
                    }
                }
            }
            recipe.Build =
                [buildResult, probes, scenePath, serializers](
                    ZoneLoadPackage& package)
                {
                    BuildScenePackage(
                        package,
                        *buildResult,
                        scenePath,
                        *serializers);
                    (void)ReadZoneProbeFile(scenePath, *probes);
                };
            recipe.Finalize =
                [this,
                 buildResult,
                 probes,
                 loggingPtr,
                 assetSystem,
                 collisionPath](
                    RuntimeWorld& runtime,
                    RuntimeZoneRecord& zone)
                {
                    (void)assetSystem;
                    if (!buildResult->Success)
                    {
                        loggingPtr->GetLogger<TemplateGame>().Error(
                            "TemplateGame: zone scene load error: {}",
                            buildResult->Error);
                        return false;
                    }
                    if (PhysicsShapes != nullptr)
                    {
                        LoadZoneCollision(
                            runtime.Entities(),
                            *PhysicsShapes,
                            collisionPath,
                            std::string(kCookedScanRoot),
                            zone.Partition);
                    }
                    if (DefaultRenderPipeline* pipeline =
                            GetEngine().GetRenderPipeline())
                    {
                        AttachZoneProbes(
                            pipeline->GetProbeVolumes(), zone, *probes);
                    }
                    return true;
                };
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
        const std::string scenePath =
            std::string(kAuthoredRoot) + "/"
            + loaded.CookedWorldSceneRef;
        const std::string collisionPath =
            std::string(kAuthoredRoot) + "/"
            + loaded.CookedWorldCollisionRef;

        ZoneLoadPackage package(kPlayZone);
        SceneBuildResult buildResult;
        BuildScenePackage(
            package,
            buildResult,
            scenePath,
            engine.SceneSerializers());
        if (!buildResult.Success)
        {
            Partition.reset();
            result.Error(
                "world scene load error: " + buildResult.Error);
            return result;
        }

        ZoneImportError importError;
        if (!ImportPackageIntoPartition(
                engine.World().Entities(),
                engine.RuntimeComponents(),
                package,
                PersistentStoragePartition,
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
                collisionPath,
                std::string(kCookedScanRoot),
                PersistentStoragePartition);
        }
        else if (!loaded.CookedWorldCollisionRef.empty())
        {
            PendingWorldSceneCollision = collisionPath;
        }
    }

    if (!PlayerPawn.IsValid())
    {
        Logger& log = logging.GetLogger<TemplateGame>();
        PlayerPawn = SpawnPlayerAvatar(
            engine.World().Entities(),
            log,
            PersistentStoragePartition,
            ResolvePlayerMovementProfile(log),
            ResolvePlayerAvatar(log));
    }

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
    RegisterMovementSystems(ctx.Schedule, RuntimeAssetState().DataAssets);
    RegisterInputSystems(
        ctx.Schedule,
        RuntimeAssetState().DataAssets,
        GetEngine().Logging());
    RegisterCameraSystem(ctx.Schedule);
    RegisterControllerSystems(ctx.Schedule);
    ctx.Schedule.Register<CharacterInputSystem>();

    // Everything that reads actions runs after they are resolved: the aim
    // integrates on the frame snapshot, the character steers on the tick record
    // along the orientation that produced.
    ctx.Schedule.After<LookIntegrationSystem, InputActionResolveSystem>();
    ctx.Schedule.After<CharacterInputSystem, LookIntegrationSystem>();
    ctx.Schedule.After<CharacterInputSystem, InputActionResolveSystem>();
    OrderMovementAfterInput<CharacterInputSystem>(ctx.Schedule);
    ctx.Schedule.Register<SpinSystem>();

    // Inert with no session: its first act each frame is to look for one.
    {
        SessionPlayerSystem& players = ctx.Schedule.Register<SessionPlayerSystem>();
        players.Owner = &GetEngine();
        players.Profile =
            ResolvePlayerMovementProfile(GetEngine().Logging().GetLogger<TemplateGame>());
        players.Avatar =
            ResolvePlayerAvatar(GetEngine().Logging().GetLogger<TemplateGame>());
        players.LocalPawn = PlayerPawn;
    }

    ctx.Schedule.Register<WorldPartitionUpdateSystem>(
        Partition,
        ZoneLoader,
        GetEngine().World(),
        PlayerPawn);
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
    if (StaticMeshComponentAssets* meshAssets =
            runtime.Entities().TryGetResource<StaticMeshComponentAssets>())
    {
        meshAssets->Meshes = nullptr;
        meshAssets->MaterialSets = nullptr;
    }
    if (AudioSourceRuntime* audioRuntime =
            runtime.Entities().TryGetResource<AudioSourceRuntime>())
    {
        audioRuntime->Clips = nullptr;
        audioRuntime->Audio = nullptr;
        audioRuntime->Captions = nullptr;
    }

    PlayerPawn = EntityId{};
    PlayZoneActive = false;
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
    // Every lease this game holds into its own data-asset cache, dropped here.
    // Declaration order alone is not enough: Assets is reset explicitly below,
    // so anything still holding a lease at that point outlives its owner and
    // calls through a destroyed vtable when the module unloads.
    PlayerMovementProfile.Reset();
    InputActionSetAsset.Reset();
    InputProfileAsset.Reset();
    // The pawns that held their own references are destroyed above, so this
    // drops the last one before the caches go away.
    ReleasePlayerAvatar();
    PlayerAvatarAsset.Reset();
    // The subtype registration holds a function pointer into this module, and
    // unregistering refuses while values are still resident, so it follows the
    // handles above and precedes the cache going away.
    if (Assets.has_value())
        UnregisterPlayerAvatarData(Assets->DataTypes, Assets->DataSchemas);
    Assets.reset();
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
    if (assets.DataAssets.Find(path).IsValid())
        return assets.DataAssets.AcquireOwned(path);

    const AssetRecord* record = assets.Registry.FindByPath(path);
    if (record == nullptr)
    {
        log.Warn("TemplateGame: '{}' is not in the asset registry", path);
        return {};
    }

    AssetStaging staged =
        assets.DataLoader.LoadStaged(*record, assets.Assets.DefaultSource());
    if (!staged.IsValid())
    {
        log.Warn("TemplateGame: '{}' failed to load: {}", path, staged.Error);
        return {};
    }

    const DataAssetHandle committed =
        assets.DataLoader.CommitTyped(std::move(staged));
    if (!committed.IsValid())
        return {};

    // CommitTyped hands over the creation reference; adopt rather than
    // re-acquire so the count stays balanced.
    return DataAssetCacheHandle(
        &assets.DataAssets, committed, DataAssetCacheHandle::NoAttach);
}

// Loads the pawn's movement profile synchronously the first time a pawn
// spawns. The asset is game-lifetime, so the owned lease lives on the game;
// the tuning system's binding cache adds its own reference on first resolve.
MovementProfileHandle TemplateGame::ResolvePlayerMovementProfile(Logger& log)
{
    if (!PlayerMovementProfile.IsValid())
        PlayerMovementProfile = AcquireDataAsset(kPlayerMovementProfilePath, log);
    return MovementProfileHandle{ PlayerMovementProfile.GetToken() };
}

// Turns the authored avatar paths into mesh and material-set handles, once.
// Every failure path leaves the result invalid, which spawns a bodyless pawn
// rather than refusing to spawn: a missing body is a content problem, not a
// reason to have no player.
ResolvedPlayerAvatar TemplateGame::ResolvePlayerAvatar(Logger& log)
{
    if (PlayerAvatar.IsValid())
        return PlayerAvatar;

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
    const StaticMeshHandle mesh =
        assets.Assets.LoadStaticMesh(avatar->MeshPath);
    if (!mesh.IsValid())
    {
        log.Warn("TemplateGame: player avatar mesh '{}' did not load",
                 avatar->MeshPath);
        return {};
    }

    std::vector<MaterialHandle> materials;
    materials.reserve(avatar->MaterialPaths.size());
    for (const std::string& path : avatar->MaterialPaths)
    {
        const MaterialHandle material = assets.Assets.LoadMaterial(path);
        if (!material.IsValid())
        {
            log.Warn("TemplateGame: player avatar material '{}' did not load",
                     path);
            for (MaterialHandle loaded : materials)
                assets.Assets.ReleaseMaterial(loaded);
            assets.Assets.ReleaseStaticMesh(mesh);
            return {};
        }
        materials.push_back(material);
    }

    const MaterialSetHandle set = assets.Assets.AcquireMaterialSet(materials);
    // The set retains its own reference to each member for its lifetime, so the
    // loads above have done their job once it exists.
    for (MaterialHandle material : materials)
        assets.Assets.ReleaseMaterial(material);
    if (!set.IsValid())
    {
        log.Warn("TemplateGame: player avatar materials did not form a set");
        assets.Assets.ReleaseStaticMesh(mesh);
        return {};
    }

    PlayerAvatar = ResolvedPlayerAvatar{ .Mesh = mesh, .Materials = set };
    return PlayerAvatar;
}

void TemplateGame::ReleasePlayerAvatar()
{
    if (!Assets.has_value())
    {
        PlayerAvatar = {};
        return;
    }

    if (PlayerAvatar.Materials.IsValid())
        Assets->Assets.ReleaseMaterialSet(PlayerAvatar.Materials);
    if (PlayerAvatar.Mesh.IsValid())
        Assets->Assets.ReleaseStaticMesh(PlayerAvatar.Mesh);
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
    SdlWindow* window =
        GetEngine().Platform().Windows.GetPrimaryWindow();
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
