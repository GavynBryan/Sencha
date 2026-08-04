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
#include <camera/CameraRegistration.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
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
#include <movement/MovementRegistration.h>
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
#include <utility>

namespace
{
constexpr std::string_view kAuthoredRoot = "assets";
constexpr std::string_view kCookedScanRoot = "assets/.cooked";
constexpr std::string_view kPlayerMovementProfilePath =
    "asset://data/player_movement.sdata";
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

EntityId SpawnPlayerAvatar(
    World& world,
    Logger& log,
    std::optional<StoragePartitionId> spawnPartition,
    MovementProfileHandle movementProfile)
{
    const Vec3d spawnPosition =
        FindPlayerStart(world, spawnPartition);

    EntityId camera = FindFirstCamera(
        world,
        PersistentStoragePartition);
    if (!camera.IsValid())
    {
        camera = CreateTransformEntity(world, spawnPosition);
        world.AddComponent<CameraComponent>(
            camera,
            CameraComponent{});
    }
    world.GetResource<ActiveCameraService>().SetActive(camera);

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

    CameraRig rig{};
    rig.Target = pawn;
    rig.Mode = CameraRigMode::FirstPerson;
    world.AddComponent<CameraRig>(camera, rig);

    log.Info(
        "TemplateGame: spawned persistent player and camera in partition zero");
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

        const InputFrame& input = ctx.Input;
        const float forward =
            (input.IsKeyDown(SDL_SCANCODE_W) ? 1.0f : 0.0f)
            - (input.IsKeyDown(SDL_SCANCODE_S) ? 1.0f : 0.0f);
        const float strafe =
            (input.IsKeyDown(SDL_SCANCODE_D) ? 1.0f : 0.0f)
            - (input.IsKeyDown(SDL_SCANCODE_A) ? 1.0f : 0.0f);

        // Held, not edge-triggered: queueing the ability every tick while the
        // key is down means a press just before landing fires on the first
        // grounded tick, and holding the key hops again on each landing. The
        // activation gate (grounded, cooldown) rejects the rest for free.
        const bool jump = input.IsKeyDown(SDL_SCANCODE_SPACE);

        float yaw = 0.0f;
        if (const ActiveCameraService* cameraService =
                world.TryGetResource<ActiveCameraService>())
        {
            if (cameraService->HasActive())
            {
                if (const CameraRig* rig = world.TryGet<CameraRig>(
                        cameraService->GetActive()))
                {
                    yaw = rig->Yaw;
                }
            }
        }

        const Quatf frame =
            Quatf::FromAxisAngle(Vec3d::Up(), yaw);
        Vec3d wish =
            frame.RotateVector(Vec3d::Forward()) * forward
            + frame.RotateVector(Vec3d::Right()) * strafe;
        wish.Y = 0.0f;
        const float squared = wish.SqrMagnitude();
        if (squared > 1.0f)
            wish = wish * (1.0f / std::sqrt(squared));

        Query<
            Write<MovementIntent>,
            Read<GameplayTagContainer>> query(world);
        query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
        {
            auto intents = view.template Write<MovementIntent>();
            const auto entityTags =
                view.template Read<GameplayTagContainer>();
            for (std::uint32_t index = 0;
                 index < view.Count();
                 ++index)
            {
                if (!entityTags[index].HasExact(tags->Controlled))
                    continue;

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
                    ResolvePlayerMovementProfile(log));
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
                std::string manifestPath = scenePath;
                constexpr std::string_view cookedSuffix = ".cooked.json";
                if (manifestPath.ends_with(cookedSuffix))
                {
                    manifestPath.resize(
                        manifestPath.size() - cookedSuffix.size());
                    manifestPath += ".manifest.json";
                    AssetManifest manifest;
                    if (Preloader.has_value()
                        && LoadAssetManifestFile(manifestPath, manifest, nullptr))
                    {
                        recipe.Preload = Preloader->Begin(ResolveManifestPaths(
                            manifest, RuntimeAssetState().Registry));
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
            ResolvePlayerMovementProfile(log));
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
    RegisterCameraSystem(ctx.Schedule);
    ctx.Schedule.Register<CharacterInputSystem>();
    OrderMovementAfterInput<CharacterInputSystem>(ctx.Schedule);
    ctx.Schedule.Register<SpinSystem>();
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

    // The world-resource binding cache holds leases into this game's
    // data-asset cache; both references must drop before Assets goes away.
    if (MovementProfileBindingCache* bindings =
            runtime.Entities().TryGetResource<MovementProfileBindingCache>())
    {
        bindings->Clear();
    }
    PlayerMovementProfile.Reset();
    Assets.reset();
}

RuntimeAssets& TemplateGame::RuntimeAssetState()
{
    assert(Assets.has_value()
           && "RuntimeAssets must be constructed before use");
    return *Assets;
}

// Loads the pawn's movement profile synchronously the first time a pawn
// spawns. The asset is game-lifetime, so the owned lease lives on the game;
// the tuning system's binding cache adds its own reference on first resolve.
// Returns an invalid handle on any failure, which the pawn treats as
// default tuning.
MovementProfileHandle TemplateGame::ResolvePlayerMovementProfile(Logger& log)
{
    if (PlayerMovementProfile.IsValid())
        return MovementProfileHandle{ PlayerMovementProfile.GetToken() };

    RuntimeAssets& assets = RuntimeAssetState();
    if (DataAssetHandle resident =
            assets.DataAssets.Find(kPlayerMovementProfilePath);
        resident.IsValid())
    {
        PlayerMovementProfile =
            assets.DataAssets.AcquireOwned(kPlayerMovementProfilePath);
        return MovementProfileHandle{ PlayerMovementProfile.GetToken() };
    }

    const AssetRecord* record =
        assets.Registry.FindByPath(kPlayerMovementProfilePath);
    if (record == nullptr)
    {
        log.Warn(
            "TemplateGame: movement profile '{}' is not in the asset "
            "registry; the pawn uses default tuning",
            kPlayerMovementProfilePath);
        return {};
    }

    AssetStaging staged =
        assets.DataLoader.LoadStaged(*record, assets.Assets.DefaultSource());
    if (!staged.IsValid())
    {
        log.Warn(
            "TemplateGame: movement profile '{}' failed to load: {}",
            kPlayerMovementProfilePath,
            staged.Error);
        return {};
    }

    const DataAssetHandle committed =
        assets.DataLoader.CommitTyped(std::move(staged));
    if (!committed.IsValid())
        return {};

    // CommitTyped hands over the creation reference; adopt rather than
    // re-acquire so the count stays balanced.
    PlayerMovementProfile = DataAssetCacheHandle(
        &assets.DataAssets, committed, DataAssetCacheHandle::NoAttach);
    return MovementProfileHandle{ PlayerMovementProfile.GetToken() };
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
