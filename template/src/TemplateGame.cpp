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
#include <movement/JumpState.h>
#include <movement/MovementComponents.h>
#include <movement/MovementIntent.h>
#include <movement/MovementProfileBindingCache.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputBindingCache.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnRecipe.h>
#include <net/NetOwnership.h>
#include <net/NetSession.h>
#include <net/PawnCommandCapture.h>
#include <net/PeerCommandRuntime.h>
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
#include <array>
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
#include <unordered_set>
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

// Everything that makes a body move and be seen, added to an entity that
// already exists. Split from SpawnPawn because a client predicting its own pawn
// needs this on an entity replication created, and a pawn that simulates on two
// machines has to be the same pawn on both -- one archetype, one place.
//
// Nothing here is about who is watching: the camera and the local input marks
// live in AttachLocalPlayer.
void BuildPawnBody(
    World& world,
    EntityId pawn,
    MovementProfileHandle movementProfile,
    const ResolvedPlayerAvatar& avatar)
{
    // Idempotent throughout. On the authority this runs on a bare entity; on a
    // client it runs on one replication has already given a transform, an
    // orientation, and a body, and adding a component twice is a structural
    // error rather than an overwrite.
    const auto ensure = [&world, pawn]<typename T>(const T& value)
    {
        if (!world.HasComponent<T>(pawn))
            world.AddComponent<T>(pawn, value);
    };

    ensure(CharacterController{});
    ensure(MovementIntent{});
    ensure(KinematicState{});
    ensure(SupportState{});
    ensure(ResolvedMovementTuning{});
    ensure(LocomotionOutput{});
    ensure(MotionAxisOverride{});
    ensure(MotionImpulse{});
    ensure(MotionRequest{});
    ensure(ModeTransitionRequest{});

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
    ensure(pawnMovement);

    // The pawn moves every tick and is what the camera watches, so it renders
    // interpolated between ticks rather than stepping at the tick rate.
    ensure(WorldTransformHistory{});

    // The body other viewers see. A first-person camera targeting this pawn
    // excludes it, so the local player does not sit inside their own mesh; a
    // third-person camera draws it. Without a resolved avatar the pawn simply
    // has no body, which is a missing asset rather than a broken player.
    if (avatar.IsValid())
    {
        ensure(StaticMeshComponent{
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
    ensure(pawnTags);

    // The profile's base layer owns the authored top speed; this attribute is
    // the effect-modifiable base and the whole answer when no profile loads,
    // so keep it at a modest speed rather than a tuned one.
    AttributeSet pawnAttributes{};
    if (movementDefs != nullptr)
        pawnAttributes.Add(movementDefs->MoveSpeed, 4.5f);
    ensure(pawnAttributes);

    // Jump is not here: it steers the body, so it lives in the movement step
    // where a predicted tick can replay it. AbilitySet is what a pawn's
    // authority-validated actions would be granted through.
    ensure(AbilitySet{});
    ensure(JumpState{});

    // The pawn aims; a camera presents it. Every pawn has an orientation --
    // a remote player is aiming somewhere too, and that is what makes their
    // body face the right way.
    ensure(LookOrientation{});
}

EntityId SpawnPawn(
    World& world,
    const Vec3d& spawnPosition,
    MovementProfileHandle movementProfile,
    const ResolvedPlayerAvatar& avatar)
{
    const EntityId pawn = CreateTransformEntity(world, spawnPosition);
    BuildPawnBody(world, pawn, movementProfile, avatar);
    return pawn;
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
void AttachLocalPlayer(World& world, EntityId pawn, ClientPrediction* prediction,
                       Logger& log)
{
    NetSetLocalControl(world, pawn, prediction);

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

    CameraRig rig{};
    rig.Target = pawn;
    rig.Mode = CameraRigMode::FirstPerson;
    if (CameraRig* existing = world.TryGet<CameraRig>(camera))
        *existing = rig;
    else
        world.AddComponent<CameraRig>(camera, rig);

    log.Info("TemplateGame: local player attached to its pawn");
}

// Builds this player a pawn at the authored start and takes possession of it.
// The pawn is not handed back: which one the player drives is the record
// AttachLocalPlayer wrote, and a second copy of that answer is the thing this
// file no longer keeps.
void SpawnPlayerAvatar(
    World& world,
    Logger& log,
    std::optional<StoragePartitionId> spawnPartition,
    MovementProfileHandle movementProfile,
    const ResolvedPlayerAvatar& avatar)
{
    const EntityId pawn = SpawnPawn(
        world, FindPlayerStart(world, spawnPartition), movementProfile, avatar);
    // No prediction: this machine defines this pawn rather than guessing ahead
    // of somebody else's answer about it.
    AttachLocalPlayer(world, pawn, nullptr, log);
}

void ConfigureRuntimeResources(
    Engine& engine,
    RuntimeAssets& assets)
{
    World& world = engine.World().Entities();

    if (StaticMeshComponentAssets* meshAssets =
            world.TryGetResource<StaticMeshComponentAssets>())
    {
        meshAssets->Meshes = assets.StaticMeshes.get();
        meshAssets->MaterialSets = &assets.MaterialSets;
    }
    else
    {
        world.AddResource<StaticMeshComponentAssets>(
            assets.StaticMeshes.get(),
            &assets.MaterialSets);
    }

    if (ZoneLightmapComponentAssets* lightmapAssets =
            world.TryGetResource<ZoneLightmapComponentAssets>())
    {
        lightmapAssets->Textures = assets.Textures.get();
    }
    else
    {
        world.AddResource<ZoneLightmapComponentAssets>(assets.Textures.get());
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
        RuntimeWorld& runtimeWorld)
        : Partition(partition)
        , Loader(loader)
        , Runtime(runtimeWorld)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Partition || !Partition->HasManifest() || !Loader)
            return;

        World& world = ctx.Entities;
        // Read rather than remembered: the pawn streaming follows is whichever
        // one the player is driving now, and joining a session replaces it.
        const EntityId pawn = LocalControlSubjectOf(world);
        if (pawn.IsValid())
        {
            if (const WorldTransform* transform =
                    world.TryGet<WorldTransform>(pawn))
            {
                Partition->SetFocus(transform->Value.Position);
            }
            if (const CharacterController* controller =
                    world.TryGet<CharacterController>(pawn))
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
        if (pawn.IsValid()
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
    std::optional<AsyncZoneLoader>& Loader;
    RuntimeWorld& Runtime;
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
                intents[index].Jump = jump;
            }
        });
    }
};

// What this game's replicated entities are. Ids match on both ends because
// both ends are the same build running the same content; they are the game's
// vocabulary, not the engine's.
enum : NetSpawnRecipeId
{
    kPlayerPawnRecipe = 1,
};

//=============================================================================
// SessionPlayerSystem
//
// Decides where this process's player pawns come from, every frame, from
// whichever side of a session it is on.
//
// Standing alone or hosting, this process provides its own pawn as soon as
// there is loaded content to put it in. Hosting, every connected peer also gets
// one, marked replicated and owned by them, and loses it when they go. As a
// client, pawns arrive as replicated state instead: this gives them the body
// every machine already has the content for and takes possession of the one
// this player owns.
//
// One decision point rather than a spawn hanging off each load callback. Two
// providers racing -- a load finishing after a join, or before it -- is how a
// client ends up driving a body the authority knows nothing about while the
// pawn it does know about walks alongside.
//=============================================================================
struct SessionPlayerSystem
{
    Engine* Owner = nullptr;
    MovementProfileHandle Profile;
    ResolvedPlayerAvatar Avatar;
    // Whether anybody is playing in this process. A dedicated host simulates
    // every pawn and owns none of them: set from the launch configuration at
    // the composition root, never inferred from whether this process can draw.
    bool ProvidesLocalPlayer = true;

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        World& world = ctx.Entities;
        NetSession* session = Owner == nullptr ? nullptr : Owner->TryNet();
        const NetSessionRole role =
            session == nullptr ? NetSessionRole::Standalone : session->Role();

        const bool replicationReady =
            world.IsRegistered<NetReplicated>() && world.IsRegistered<NetOwner>();

        if (role == NetSessionRole::Client)
        {
            // The authority owns every pawn in a session, this player's
            // included. Providing one here as well is the second provider.
            if (replicationReady && session->IsConnected())
                FollowLocalControl(world);
            return;
        }

        ProvideLocalPawn(world);

        if (role == NetSessionRole::Host && replicationReady)
            ServePeers(world, *session);
    }

private:
    Logger& Log() { return Owner->Logging().GetLogger<TemplateGame>(); }

    // The pawn this process's player drives, when providing it is this
    // process's job. Nothing is placed before a load publishes somewhere to
    // put it, and nothing is placed on top of a pawn that already exists --
    // which is the same check whether the last one came from a load or from a
    // session this process has since left.
    void ProvideLocalPawn(World& world)
    {
        // A dedicated host has nobody to provide one for. Everything a pawn is
        // for here -- possession, the look input that steers it, the camera it
        // is presented through -- describes a player at this machine.
        if (!ProvidesLocalPlayer)
            return;

        const PlayContentPartition* content =
            world.TryGetResource<PlayContentPartition>();
        if (content == nullptr)
            return;
        if (LocalControlSubjectOf(world).IsValid())
            return;

        SpawnPlayerAvatar(world, Log(), content->Value, Profile, Avatar);
    }

    void ServePeers(World& world, NetSession& session)
    {
        // The host is a player too. Without this a client would see everyone
        // except the person running the server, which is the one pawn they are
        // most likely to be standing next to. Read each frame rather than
        // remembered, because the pawn appears when content loads and that can
        // be after this system is registered.
        const EntityId local = LocalControlSubjectOf(world);
        if (local.IsValid() && !world.HasComponent<NetReplicated>(local))
        {
            world.AddComponent<NetReplicated>(local);
            world.AddComponent<NetSpawnRecipe>(
                local, NetSpawnRecipe{ .Id = kPlayerPawnRecipe });
        }

        const std::vector<PeerId> peers = session.ConnectedPeers();

        std::vector<EntityId> owned;
        for (PeerId peer : peers)
        {
            // Asked of the component that already answers it. A map beside it
            // would be a second copy of the same fact, and the copy is what
            // goes stale the first time ownership moves.
            owned.clear();
            NetOwnedBy(world, peer, owned);
            if (!owned.empty())
                continue;

            // Offset laterally from the authored start so two players do not
            // arrive inside each other, by peer id so a peer lands in the same
            // place however many others are present. A proper multi-start
            // rotation is the level's business, not this system's.
            //
            // Unfiltered: a map's content is imported into its own zone
            // partition, so a start looked for only in the persistent one is a
            // start that is never found and a peer that arrives at the origin.
            Vec3d spawn = FindPlayerStart(world, std::nullopt);
            spawn.X += 2.0f * static_cast<float>(peer.Value);

            const EntityId pawn = SpawnPawn(world, spawn, Profile, Avatar);
            world.AddComponent<NetReplicated>(pawn);
            world.AddComponent<NetSpawnRecipe>(
                pawn, NetSpawnRecipe{ .Id = kPlayerPawnRecipe });
            // Whose pawn this is, and everything that follows from it: whose
            // aim turns it, whose keys move it, and whose snapshot carries the
            // state only its owner may see.
            NetSetOwner(world, pawn, peer);
            Log().Info("TemplateGame: spawned a pawn for peer {}", peer.Value);
        }

        // A peer that left takes its pawn with it. The engine hands the
        // entities a departing peer owned back to the authority, so what is
        // left behind is a player pawn nobody drives -- which the host's own
        // pawn also looks like, and is why that one is excluded by name.
        std::vector<EntityId> orphans;
        const World& reading = world;
        reading.ForEachComponent<NetSpawnRecipe>(
            [&](EntityId entity, const NetSpawnRecipe& recipe) {
                if (recipe.Id != kPlayerPawnRecipe || entity == local)
                    return;
                if (NetOwnerOf(world, entity).IsValid())
                    return;
                orphans.push_back(entity);
            });

        for (const EntityId orphan : orphans)
        {
            if (!world.IsAlive(orphan))
                continue;
            world.DestroyEntity(orphan);
            Log().Info("TemplateGame: removed the pawn for peer that left");
        }
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

        // A pawn this process provided for itself before joining is now
        // somebody else's job to simulate, and leaving it would leave a second
        // body standing where the player used to be. Local only: a replicated
        // entity destroyed here would come back on the next snapshot without
        // the fields that have not changed since.
        if (Followed.IsValid() && world.IsAlive(Followed)
            && !world.HasComponent<NetReplicated>(Followed))
        {
            world.DestroyEntity(Followed);
        }
        Followed = subject;

        if (!subject.IsValid())
            return;

        // This pawn becomes a full simulation participant on this machine. A
        // player holding a key cannot wait for the round trip to see it, so the
        // client runs the same systems over the same input and the authority's
        // snapshots become something to reconcile against rather than obey.
        //
        // The same archetype the authority built, from the same function: two
        // machines simulating one pawn from the same input have to be
        // simulating the same pawn.
        if (!world.HasComponent<MovementIntent>(subject))
            BuildPawnBody(world, subject, Profile, Avatar);

        AttachLocalPlayer(world, subject, &Owner->Prediction(), Log());
        Log().Info("TemplateGame: predicting this player's own pawn");
    }

    // The pawn this machine was last told to drive, so taking up a new one is
    // an edge rather than something re-derived every frame.
    EntityId Followed;

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
            graphics->Samplers);
    }
    else
    {
        Assets.emplace(logging);
    }
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // This game's own data subtypes, registered into the registries it owns and
    // unregistered in OnShutdown while the module is still mapped: the registry
    // holds function pointers into this module.
    RegisterPlayerAvatarData(runtimeAssets.DataTypes, runtimeAssets.DataSchemas);

    // What a replicated player pawn becomes on whichever machine receives it.
    // A snapshot brings the state; this brings everything a body needs to be
    // seen, which is content both ends already have and neither has to be told
    // about. The avatar is resolved once here rather than per spawn.
    const bool pawnRecipeRegistered = engine.SpawnRecipes().Register(
        kPlayerPawnRecipe,
        [this](World& world, EntityId entity) {
            Logger& log = GetEngine().Logging().GetLogger<TemplateGame>();
            const ResolvedPlayerAvatar avatar = ResolvePlayerAvatar(log);
            if (avatar.IsValid() && !world.HasComponent<StaticMeshComponent>(entity))
            {
                world.AddComponent<StaticMeshComponent>(
                    entity,
                    StaticMeshComponent{ .Mesh = avatar.Mesh,
                                         .Materials = avatar.Materials });
            }
            // Pawns move every tick, so they present interpolated between
            // ticks rather than stepping at the tick rate.
            if (!world.HasComponent<WorldTransformHistory>(entity))
            {
                world.AddComponent<WorldTransformHistory>(
                    entity, WorldTransformHistory{});
            }

            // Which profile a pawn resolves tuning from is content this machine
            // already has, and a handle into its own asset cache is meaningless
            // on any other -- so the field does not travel, and naming it is
            // this side's job. It has to happen here rather than when the local
            // player takes possession: replication has already created
            // CharacterMovement by the time a recipe runs, and the possession
            // path only adds components that are missing, so a profile written
            // there would be dropped on exactly the machine that predicts. A
            // pawn left on the default handle resolves engine tuning while the
            // authority runs the authored kind, and the two simulations then
            // disagree by design on every input.
            const MovementProfileHandle profile = ResolvePlayerMovementProfile(log);
            if (CharacterMovement* movement =
                    world.TryGet<CharacterMovement>(entity))
            {
                // Mode is the authority's word and arrives on the wire; only
                // the profile is this machine's to fill in.
                movement->Profile = profile;
            }
            else
            {
                CharacterMovement built;
                built.Profile = profile;
                if (const LocomotionModeRegistry* modes =
                        world.TryGetResource<LocomotionModeRegistry>())
                {
                    built.Mode = modes->FreeMode();
                }
                world.AddComponent<CharacterMovement>(entity, built);
            }

            log.Info("TemplateGame: built a replicated player pawn");
        });
    if (!pawnRecipeRegistered)
    {
        // Nothing downstream can tell this apart from an authority running
        // content this build does not have, so it is said here, where the id is
        // known to be ours and the cause is a second claim on it.
        GetEngine().Logging().GetLogger<TemplateGame>().Error(
            "TemplateGame: spawn recipe {} was already registered; replicated "
            "player pawns will arrive without a body",
            static_cast<unsigned>(kPlayerPawnRecipe));
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

            // Where a pawn belongs, not a pawn. Who provides one is the
            // session's decision, taken every frame once this exists: a load
            // that finished after a join would otherwise place a second body
            // beside the one the authority is already simulating.
            PublishPlayContent(runtime.Entities(), zone.Partition);
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

    // A world's scene imports into the persistent partition, so that is where
    // a pawn belongs. Providing one is the session's decision.
    PublishPlayContent(engine.World().Entities(), PersistentStoragePartition);

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

    // Waits on content with no session, and on the authority with one: either
    // way its first act each frame is to ask where this player's pawn comes
    // from.
    {
        Logger& log = GetEngine().Logging().GetLogger<TemplateGame>();
        SessionPlayerSystem& players = ctx.Schedule.Register<SessionPlayerSystem>();
        players.Owner = &GetEngine();
        // The authority simulates movement whether or not anyone is watching,
        // so the profile is resolved in every configuration.
        players.Profile = ResolvePlayerMovementProfile(log);
        players.ProvidesLocalPlayer = GetEngine().Config().Runtime.HasLocalPlayer;
        // Resolves to no body on a process that cannot hold a mesh, which is
        // exactly what a bodyless pawn wants.
        players.Avatar = ResolvePlayerAvatar(log);
    }

    WorldPartitionUpdateSystem& partitionUpdate =
        ctx.Schedule.Register<WorldPartitionUpdateSystem>(
            Partition,
            ZoneLoader,
            GetEngine().World());
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
    // The spawn recipe is a callable whose target lives in this module, for the
    // same reason the subtype registration below is: it has to go while the
    // module is still mapped.
    GetEngine().SpawnRecipes().Clear();
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
