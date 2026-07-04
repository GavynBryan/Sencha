#include "TemplateGame.h"

#include "SpinComponent.h"

#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/GameModule.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/assets/AssetRegistry.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/ComponentTypeId.h>
#include <abilities/AbilityKit.h>
#include <camera/CameraRegistration.h>
#include <camera/CameraRig.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementState.h>
#include <movement/MovementTags.h>
#include <movement/MovementRegistration.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/ZoneCollisionLoader.h>
#include <physics/components/CharacterController.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <render/Camera.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/DefaultZoneBuilder.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneRuntime.h>

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

namespace
{
    // Content layout: authored assets under "assets", cooked artifacts under
    // "assets/.cooked". The authored scan skips .cooked on its own; the cooked
    // scan is a second root so each generated mesh maps to the asset:// path the
    // cook stamped. The host runs with the project directory as CWD.
    constexpr std::string_view kAuthoredRoot = "assets";
    constexpr std::string_view kCookedScanRoot = "assets/.cooked";
    constexpr ZoneId kPlayZone{ 1 };

    struct SceneParse
    {
        std::optional<JsonValue> Json;
        std::string Error;
    };

    SceneParse ParseSceneFile(const std::string& path)
    {
        SceneParse out;
        std::ifstream file(path);
        if (!file.is_open())
        {
            out.Error = "could not open scene file '" + path + "'";
            return out;
        }
        std::ostringstream buf;
        buf << file.rdbuf();

        JsonParseError parseError;
        out.Json = JsonParse(buf.str(), &parseError);
        if (!out.Json)
            out.Error = "scene JSON parse error at " + std::to_string(parseError.Position)
                + ": " + parseError.Message;
        return out;
    }

    EntityId FindFirstCamera(Registry& registry)
    {
        for (EntityId entity : registry.Entities.GetAliveEntities())
            if (registry.Components.TryGet<CameraComponent>(entity) != nullptr)
                return entity;
        return EntityId{};
    }

    // The task-thread half of loading one cooked zone scene: component and
    // resource registration on the detached registry, then the JSON parse.
    // Shared by the single-zone map path and the world-streaming recipe.
    void BuildZoneScene(Registry& registry, SceneParse& parsed, const std::string& scenePath,
                        StaticMeshCache* meshes, MaterialSetCache* materialSets)
    {
        InitializeDefault3DRegistry(registry, meshes, materialSets);
        // Physics components must be registered before any entity is created
        // in this zone's World (build runs before finalize spawns entities).
        // The helper also registers the runtime link components the bridges
        // add at reconcile time, so they cannot be forgotten.
        RegisterPhysicsComponents(registry.Components);
        // Gameplay components (movement, tags, attributes, abilities, camera
        // rig) plus their resources, registered here for the same reason:
        // storage must exist before the finalize pass spawns entities.
        RegisterMovement(registry.Components);
        RegisterCameraComponents(registry.Components);
        parsed = ParseSceneFile(scenePath);
    }

    // The owner-thread half: scene deserialization plus the cooked brush
    // collision. Returns false when the scene did not load.
    bool FinalizeZoneScene(Registry& registry, const SceneParse& parsed,
                           LoggingProvider& logging, AssetSystem* assets,
                           CollisionShapeCache* shapes, const std::string& collisionSidecar)
    {
        Logger& log = logging.GetLogger<TemplateGame>();
        if (!parsed.Json)
        {
            log.Error("TemplateGame: {}", parsed.Error);
            return false;
        }

        SceneLoadError loadError;
        SceneSerializationContext sceneContext(logging, assets);
        if (!LoadSceneJson(*parsed.Json, registry, sceneContext, &loadError))
        {
            log.Error("TemplateGame: scene load error: {}", loadError.Message);
            return false;
        }

        // Load the level's cooked brush collision: spawns the static colliders
        // the character walks on. No collision authoring; it rode the cook.
        if (shapes != nullptr)
            LoadZoneCollision(registry.Components, *shapes, collisionSidecar,
                              std::string(kCookedScanRoot));
        return true;
    }

    // Camera plus the controlled pawn: world-lifetime avatar state, not zone
    // content. The map path spawns it into its single zone; the world path
    // spawns it once into the global registry, where zone unloads never
    // touch it. Returns the pawn.
    EntityId SpawnPlayerAvatar(Registry& registry)
    {
        // Use the scene's camera if it authored one; a cooked level is pure
        // geometry, so spawn one to make it viewable.
        EntityId camera = FindFirstCamera(registry);
        if (!camera.IsValid())
        {
            Transform3f cameraStart;
            cameraStart.Position = Vec3d{ 0.0f, 2.0, 0.0f };
            camera = CreateDefaultEntity(registry, cameraStart);
            AddDefaultCamera(registry, camera, CameraComponent{}, /*makeActive*/ true);
        }
        else
        {
            registry.Resources.Get<ActiveCameraService>().SetActive(camera);
        }

        // The player is a kinematic capsule the camera follows, a separate
        // entity from the camera. Locomotion is data on the pawn: intent +
        // profile + a mode marker, top speed as the MoveSpeed attribute, jump
        // as a granted ability. The framework resolves intent to a planar
        // DesiredVelocity; the engine's CharacterControllerSystem resolves that
        // against collision.
        World& pawnWorld = registry.Components;
        Transform3f pawnStart;
        pawnStart.Position = Vec3d{ 0.0f, 2.0f, 0.0f };
        const EntityId pawn = CreateDefaultEntity(registry, pawnStart);
        pawnWorld.AddComponent<CharacterController>(pawn, CharacterController{});
        pawnWorld.AddComponent<MovementProfile>(pawn, MovementProfile{});
        pawnWorld.AddComponent<MovementState>(pawn, MovementState{});
        pawnWorld.AddComponent<MovementIntent>(pawn, MovementIntent{});
        pawnWorld.AddComponent<OnGround>(pawn, OnGround{});
        // The mode arbiter projects movement.grounded (which gates the jump
        // ability) only for pawns carrying a mode request. Without it the pawn
        // walks (that only needs the OnGround marker) but can never jump.
        pawnWorld.AddComponent<LocomotionModeRequest>(pawn, LocomotionModeRequest{});

        const MovementDefs* movementDefs = pawnWorld.TryGetResource<MovementDefs>();

        GameplayTagContainer pawnTags{};
        if (const MovementTags* movementTags = pawnWorld.TryGetResource<MovementTags>())
            pawnTags.Grant(movementTags->Controlled);
        pawnWorld.AddComponent<GameplayTagContainer>(pawn, pawnTags);

        AttributeSet pawnAttributes{};
        if (movementDefs != nullptr)
            pawnAttributes.Add(movementDefs->MoveSpeed, 2.0f);
        pawnWorld.AddComponent<AttributeSet>(pawn, pawnAttributes);

        AbilitySet pawnAbilities{};
        if (movementDefs != nullptr)
            pawnAbilities.Grant(movementDefs->Jump);
        pawnWorld.AddComponent<AbilitySet>(pawn, pawnAbilities);

        // Point the camera at the pawn. Mode is data: first-person here, but
        // third-person and fixed are the same system on a different value.
        CameraRig rig{};
        rig.Target = pawn;
        rig.Mode = CameraRigMode::FirstPerson;
        registry.Components.AddComponent<CameraRig>(camera, rig);
        return pawn;
    }

    // Feeds the pawn's position to the partition focus, then pumps the
    // streaming policy: the once-per-frame Update the runtime's contract
    // requires, game-pumped by design. Inert until `world` loads a manifest.
    struct WorldPartitionUpdateSystem
    {
        WorldPartitionUpdateSystem(std::optional<WorldPartitionRuntime>& partition,
                                   std::optional<AsyncZoneLoader>& loader,
                                   ZoneRuntime*& zones, EntityId& pawn)
            : Partition(partition)
            , Loader(loader)
            , Zones(zones)
            , Pawn(pawn)
        {
        }

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (!Partition || !Partition->HasManifest() || !Loader || Zones == nullptr)
                return;
            if (Pawn.IsValid())
            {
                const World& world = Zones->Global().Components;
                if (world.IsRegistered<WorldTransform>())
                    if (const WorldTransform* transform = world.TryGet<WorldTransform>(Pawn))
                        Partition->SetFocus(transform->Value.Position);
            }
            Partition->Update(ctx.WallDeltaSeconds, *Loader, *Zones);
        }

        std::optional<WorldPartitionRuntime>& Partition;
        std::optional<AsyncZoneLoader>& Loader;
        ZoneRuntime*& Zones;
        EntityId& Pawn;
    };

    // Player input -> MovementIntent + ability activations for the controlled
    // pawn(s). Reads the active camera rig's yaw so WASD is camera-relative; the
    // locomotion operations then consume the world-space intent, decoupled from
    // input and the camera. Discrete actions (jump) are queued as ability
    // activations, gated downstream by AbilityKit (grounded, cooldown), not here.
    struct CharacterInputSystem
    {
        explicit CharacterInputSystem(Registry*& registry)
            : RegistryInstance(registry)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;
            World& world = RegistryInstance->Components;
            if (!world.IsRegistered<MovementIntent>() || !world.IsRegistered<GameplayTagContainer>())
                return;
            const MovementTags* ids = world.TryGetResource<MovementTags>();
            const MovementDefs* defs = world.TryGetResource<MovementDefs>();
            AbilityActivationQueue* activations = world.TryGetResource<AbilityActivationQueue>();
            if (ids == nullptr || defs == nullptr)
                return;

            const InputFrame& input = ctx.Input;
            const float forward = (input.IsKeyDown(SDL_SCANCODE_W) ? 1.0f : 0.0f)
                                - (input.IsKeyDown(SDL_SCANCODE_S) ? 1.0f : 0.0f);
            const float strafe = (input.IsKeyDown(SDL_SCANCODE_D) ? 1.0f : 0.0f)
                               - (input.IsKeyDown(SDL_SCANCODE_A) ? 1.0f : 0.0f);
            bool jump = false;
            for (std::uint32_t scancode : input.KeysPressed)
                if (scancode == SDL_SCANCODE_SPACE)
                {
                    jump = true;
                    break;
                }

            float yaw = 0.0f;
            if (const ActiveCameraService* cameraService =
                    RegistryInstance->Resources.TryGet<ActiveCameraService>())
                if (cameraService->HasActive() && world.IsRegistered<CameraRig>())
                    if (const CameraRig* rig = world.TryGet<CameraRig>(cameraService->GetActive()))
                        yaw = rig->Yaw;

            const Quatf frame = Quatf::FromAxisAngle(Vec3d::Up(), yaw);
            Vec3d wish = frame.RotateVector(Vec3d::Forward()) * forward
                       + frame.RotateVector(Vec3d::Right()) * strafe;
            wish.Y = 0.0f;
            const float sqr = wish.SqrMagnitude();
            if (sqr > 1.0f)
                wish = wish * (1.0f / std::sqrt(sqr));

            world.ForEachComponent<MovementIntent>([&](EntityId entity, MovementIntent& intent)
            {
                const GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(entity);
                if (tags == nullptr || !tags->HasExact(ids->Controlled))
                    return;
                intent.WishDir = wish;
                if (jump && activations != nullptr)
                    activations->Pending.push_back({ entity, defs->Jump });
            });
        }

        Registry*& RegistryInstance;
    };

    // A game system: rotates every entity carrying a SpinComponent. The example
    // of gameplay reading a game-defined component placed in the editor and baked
    // through the cook. Mutates transform values only, never archetype membership.
    struct SpinSystem
    {
        explicit SpinSystem(Registry*& registry)
            : RegistryInstance(registry)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;
            World& world = RegistryInstance->Components;
            if (!world.IsRegistered<SpinComponent>())
                return;

            const float dt = static_cast<float>(ctx.Time.DeltaSeconds);
            world.ForEachComponent<SpinComponent>([&](EntityId id, SpinComponent& spin) {
                LocalTransform* transform = world.TryGet<LocalTransform>(id);
                if (transform == nullptr)
                    return;
                transform->Value.Rotation =
                    transform->Value.Rotation
                    * Quatf::FromAxisAngle(Vec3d::Up(), spin.RadiansPerSecond * dt);
            });
        }

        Registry*& RegistryInstance;
    };
} // namespace

void TemplateGame::OnRegisterComponents(ComponentSerializerRegistry&)
{
    // Built-in scene components (Transform, StaticMesh, Camera, ...) plus the
    // game's own. RegisterComponent<T>() is schema-driven, so SpinComponent
    // serializes through the cook and shows up editable in the editor inspector.
    // The editor reuses this hook to edit scenes containing the game's components
    // without ever starting the game.
    InitSceneSerializer();
    RegisterComponent<SpinComponent>();
}

void TemplateGame::OnUnregisterComponents(ComponentSerializerRegistry& serializers)
{
    // Symmetric teardown: retract the serializers whose code lives in THIS module
    // before the host unmaps it. Without this, the SpinComponent serializer would
    // outlive dlclose and crash when the registry frees it at exit. Built-in
    // serializers (engine code) are left for the host to manage.
    serializers.Remove(ResolveComponentTypeId<SpinComponent>());
}

void TemplateGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(logging, graphics.Buffers, graphics.Images, graphics.Descriptors, graphics.Samplers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // Mount: authored assets, then the cooked overlay (cooked wins). The cooked
    // index adds artifacts the physical scan cannot key, notably cooked textures
    // (asset://...png serving cooked .stex bytes).
    ScanAssetsDirectory(std::string(kAuthoredRoot), runtimeAssets.Registry);
    ScanAssetsDirectory(std::string(kCookedScanRoot), runtimeAssets.Registry);
    RegisterCookedAssets(std::string(kAuthoredRoot), runtimeAssets.Registry);

    {
        AssetIdMap idMap;
        std::string idMapError;
        const std::string idMapPath =
            std::string(kAuthoredRoot) + "/" + std::string(kAssetIdMapFileName);
        if (AssetIdMap::LoadFromFile(idMapPath, idMap, &idMapError))
            ApplyAssetIds(idMap, runtimeAssets.Registry);
        else
            logging.GetLogger<TemplateGame>().Warn(
                "TemplateGame: no asset id map ({}); refs resolve by path only", idMapError);
    }

    ZoneLoader.emplace(engine.Tasks(), engine.Zones(), engine.Runtime());
    Preloader.emplace(logging, runtimeAssets.Registry, runtimeAssets.Assets, engine.Tasks());

    if (DefaultRenderPipeline* pipeline = engine.GetRenderPipeline())
    {
        pipeline->SetAssetStores(
            runtimeAssets.StaticMeshes, runtimeAssets.Materials, runtimeAssets.MaterialSets);
        pipeline->AddMeshRenderFeature(graphics);
    }

    ZoneRuntimePtr = &engine.Zones();

    // The +map mechanism: the startup script runs `map <name>` immediately after
    // this hook (ConsolePhase::GameLoaded), landing in LoadMap. The `world` and
    // `zone` commands ride the same mechanism: +world/+zone argv become console
    // lines executed at GameLoaded (ConsoleStartupScript::FromArgv).
    engine.Console().SetMapHandler(
        [this](std::string_view mapName) { return LoadMap(mapName); });
    engine.Console().Registry().RegisterCommand({
        .Name = "world",
        .Owner = "game",
        .Usage = "world <name>",
        .Help = "Load a cooked partitioned world (assets/.cooked/worlds/<name>.sworld.json) "
                "and stream its zones around the player.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
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
        .Help = "Focus the loaded world on a zone; queued when issued before `world`.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: zone <16-hex zone id>");
                return usage;
            }
            return FocusWorldZone(args[0]);
        },
    });

    std::printf("Sencha game template\n");
    std::printf("  Load a map: +map levels/<name> (cooked under assets/.cooked/)\n");
    std::printf("  Load a world: +world <name> (+zone <hexid> overrides the start zone)\n");
    std::printf("  Right mouse: look | WASD: move | Space: jump | Escape: quit\n");
}

ConsoleResult TemplateGame::LoadMap(std::string_view mapName)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    RuntimeAssets& runtimeAssets = RuntimeAssetState();
    Logger& log = logging.GetLogger<TemplateGame>();

    const std::string base = std::string(kCookedScanRoot) + "/" + std::string(mapName);
    const std::string scenePath = base + ".cooked.json";
    const std::string manifestPath = base + ".manifest.json";
    const std::string collisionSidecar = base + ".collision.json";

    // Re-map: drop the in-flight load or the committed zone before loading anew.
    if (ZoneLoader)
    {
        if (ZoneLoader->IsLoading(kPlayZone))
            ZoneLoader->CancelLoad(kPlayZone);
        if (ZoneActive)
            engine.Zones().DestroyZone(kPlayZone);
    }
    ActiveZoneRegistry = nullptr;
    ZoneActive = false;

    // Manifest-driven preload (optional): a missing manifest is the sync
    // resolve-on-attach fallback.
    std::shared_ptr<AssetPreload> preload;
    AssetManifest manifest;
    std::string manifestError;
    if (LoadAssetManifestFile(manifestPath, manifest, &manifestError))
        preload = Preloader->Begin(ResolveManifestPaths(manifest, runtimeAssets.Registry));
    else
        log.Warn("TemplateGame: no manifest for '{}' ({}); resolve-on-attach",
                 std::string(mapName), manifestError);

    auto parsed = std::make_shared<SceneParse>();
    StaticMeshCache* meshes = &runtimeAssets.StaticMeshes;
    MaterialSetCache* materialSets = &runtimeAssets.MaterialSets;

    ZoneLoader->BeginLoad(
        kPlayZone,
        [parsed, meshes, materialSets, scenePath](Registry& registry) {
            BuildZoneScene(registry, *parsed, scenePath, meshes, materialSets);
        },
        [this, parsed, &logging, collisionSidecar](Registry& registry) {
            if (!FinalizeZoneScene(registry, *parsed, logging, &RuntimeAssetState().Assets,
                                   PhysicsShapes, collisionSidecar))
                return;

            // Single-zone life: the avatar lives and dies with the map's zone.
            (void)SpawnPlayerAvatar(registry);

            ActiveZoneRegistry = &registry;
            ZoneActive = true;
        },
        ZoneParticipation{ .Visible = true, .Physics = true, .Logic = true, .Audio = true },
        std::move(preload));

    ConsoleResult result;
    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

ConsoleResult TemplateGame::LoadWorld(std::string_view worldName)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    ConsoleResult result;

    // One world at a time, in either direction: mixing the single-zone map
    // path with streamed zones would race two owners over the play space.
    if (ZoneActive || (ZoneLoader && ZoneLoader->IsLoading(kPlayZone)))
    {
        result.Error("a map is loaded; restart and use +world " + std::string(worldName));
        return result;
    }
    if (Partition && Partition->HasManifest())
    {
        result.Error("a world is already loaded; restart to switch worlds");
        return result;
    }

    const std::string manifestPath = std::string(kCookedScanRoot) + "/worlds/"
        + std::string(worldName) + ".sworld.json";
    std::ifstream file(manifestPath);
    if (!file.is_open())
    {
        result.Error("no cooked world manifest at '" + manifestPath + "'; cook the world first");
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    JsonParseError parseError;
    const auto json = JsonParse(buffer.str(), &parseError);
    if (!json)
    {
        result.Error("world manifest parse error at " + std::to_string(parseError.Position)
                     + ": " + parseError.Message);
        return result;
    }
    std::string manifestError;
    auto manifest = ReadWorldPartitionManifest(*json, &manifestError);
    if (!manifest)
    {
        result.Error("world manifest rejected: " + manifestError);
        return result;
    }

    RuntimeAssets& runtimeAssets = RuntimeAssetState();
    StaticMeshCache* meshes = &runtimeAssets.StaticMeshes;
    MaterialSetCache* materialSets = &runtimeAssets.MaterialSets;
    AssetSystem* assets = &runtimeAssets.Assets;
    CollisionShapeCache* shapes = PhysicsShapes;
    LoggingProvider* loggingPtr = &logging;

    const EngineRuntimeConfig& runtimeConfig = engine.Config().Runtime;
    Partition.emplace(
        [meshes, materialSets, assets, shapes, loggingPtr](const ZoneHeader& header)
        {
            // Cooked refs are relative to the assets root (the world cook's
            // contract); the recipe carries scene and collision only. The
            // avatar is world-lifetime state and never part of a zone recipe.
            const std::string scenePath =
                std::string(kAuthoredRoot) + "/" + header.CookedSceneRef;
            const std::string collisionPath =
                std::string(kAuthoredRoot) + "/" + header.CookedCollisionRef;
            auto parsed = std::make_shared<SceneParse>();
            ZoneLoadRecipe recipe;
            recipe.Build = [parsed, scenePath, meshes, materialSets](Registry& registry)
            { BuildZoneScene(registry, *parsed, scenePath, meshes, materialSets); };
            recipe.Finalize = [parsed, loggingPtr, assets, shapes, collisionPath](Registry& registry)
            { (void)FinalizeZoneScene(registry, *parsed, *loggingPtr, assets, shapes, collisionPath); };
            return recipe;
        },
        WorldPartitionStreamingConfig{
            .HopCount = runtimeConfig.StreamingHopCount,
            .LingerSeconds = runtimeConfig.StreamingLingerSeconds,
            .ResidentZoneCap = runtimeConfig.StreamingResidentZoneCap,
        });

    std::string loadError;
    if (!Partition->LoadManifest(std::move(*manifest), &loadError))
    {
        Partition.reset();
        result.Error("world refused: " + loadError);
        return result;
    }

    // The avatar spawns once into the global registry, so zone unloads never
    // destroy it. The global registry needs the same component registration a
    // zone build performs before entities exist in it.
    if (!WorldPawn.IsValid())
    {
        Registry& global = engine.Zones().Global();
        InitializeDefault3DRegistry(global, meshes, materialSets);
        RegisterPhysicsComponents(global.Components);
        RegisterMovement(global.Components);
        RegisterCameraComponents(global.Components);
        WorldPawn = SpawnPlayerAvatar(global);
        ActiveZoneRegistry = &global;   // the input and spin systems read this
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
    else
        result.Warning("world '" + std::string(worldName)
                       + "' designates no start zone; focus follows the pawn");

    result.Info("loading world '" + std::string(worldName) + "'");
    return result;
}

ConsoleResult TemplateGame::FocusWorldZone(std::string_view zoneHex)
{
    ConsoleResult result;
    const auto zone = ZoneIdFromString(zoneHex);
    if (!zone.has_value())
    {
        result.Error("malformed zone id '" + std::string(zoneHex)
                     + "' (16 lowercase hex digits, nonzero)");
        return result;
    }

    if (Partition && Partition->HasManifest())
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
            if (header.Id == *zone)
            {
                Partition->SetFocus(*zone);
                result.Info("focus zone " + std::string(zoneHex));
                return result;
            }
        result.Error("zone " + std::string(zoneHex) + " is not in the loaded world");
        return result;
    }

    // Before `world` takes effect (the +world +zone argv order is not
    // guaranteed): queue the override for the world load to consume.
    PendingZoneFocus = *zone;
    result.Info("zone focus queued for the next world load");
    return result;
}

void TemplateGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    RegisterPhysics(ctx.Schedule);
    // The physics step system owns the shared collision cache; grab it so map
    // load can fill it with the level's cooked brush collision.
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        PhysicsShapes = &step->GetShapeCache();
    // Engine-provided gameplay: the ability kit and movement systems drive the sim
    // over active registries, and camera follow places the view. The template only
    // wires input in and orders it ahead of the sim so intent lands the same tick.
    RegisterAbilityKitSystems(ctx.Schedule);
    RegisterMovementSystems(ctx.Schedule);
    RegisterCameraSystem(ctx.Schedule);
    ctx.Schedule.Register<CharacterInputSystem>(ActiveZoneRegistry);
    OrderMovementAfterInput<CharacterInputSystem>(ctx.Schedule);
    ctx.Schedule.Register<SpinSystem>(ActiveZoneRegistry);
    ctx.Schedule.Register<WorldPartitionUpdateSystem>(Partition, ZoneLoader, ZoneRuntimePtr,
                                                      WorldPawn);
}

void TemplateGame::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Handled)
        return;

    if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
        SetRelativeMouseMode(true);
    else if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
        SetRelativeMouseMode(false);
    else if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        SetRelativeMouseMode(false);
}

void TemplateGame::OnShutdown(GameShutdownContext&)
{
    SetRelativeMouseMode(false);

    // World path: drop in-flight loads and destroy streamed zones so their
    // mesh and texture handles return to the caches before the caches die.
    if (Partition && Partition->HasManifest() && ZoneLoader)
    {
        for (const ZoneHeader& zone : Partition->Manifest().Zones)
        {
            if (ZoneLoader->IsLoading(zone.Id))
                (void)ZoneLoader->CancelLoad(zone.Id);
            if (GetEngine().Zones().IsZoneLoaded(zone.Id))
                (void)GetEngine().Zones().DestroyZone(zone.Id);
        }
    }
    Partition.reset();

    if (ZoneLoader && ZoneLoader->IsLoading(kPlayZone))
        ZoneLoader->CancelLoad(kPlayZone);
    ZoneLoader.reset();

    GetEngine().Zones().DestroyZone(kPlayZone);
    ActiveZoneRegistry = nullptr;

    // Release the GPU-backed asset caches here, while OnShutdown still runs with
    // the engine (device, allocators, descriptor pools) up. DestroyZone above
    // already returned the zone's mesh/texture handles to these caches. Left to
    // the module-static Game's own destruction, they would free at process exit
    // after the device is gone, corrupting the heap on a clean window close (PIE
    // never hit this: Stop kills the process, so no exit handlers run).
    Preloader.reset();
    Assets.reset();
}

RuntimeAssets& TemplateGame::RuntimeAssetState()
{
    assert(Assets.has_value() && "RuntimeAssets must be constructed before use");
    return *Assets;
}

void TemplateGame::SetRelativeMouseMode(bool enabled)
{
    SdlWindow* window = GetEngine().Platform().Windows.GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;
    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) == enabled)
        return;
    SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);
}

//=============================================================================
// Game module entry points (the only exported symbols).
//=============================================================================
extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    // Module-owned static: the host drives it but never deletes it across the
    // allocator boundary; teardown is OnShutdown + unmap.
    static TemplateGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
