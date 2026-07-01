#include "TemplateGame.h"

#include "SpinComponent.h"

#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/GameModule.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/ComponentTypeId.h>
#include <framework/AbilityKit.h>
#include <framework/camera/CameraRig.h>
#include <framework/movement/MovementDefs.h>
#include <framework/movement/MovementIntent.h>
#include <framework/movement/MovementModes.h>
#include <framework/movement/MovementProfile.h>
#include <framework/movement/MovementState.h>
#include <framework/movement/MovementSystems.h>
#include <framework/movement/MovementTags.h>
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

    // Fixed-tick pump for the registered gameplay framework on the active logic
    // registry. The per-mode logic lives in the framework; the composition layer
    // owns only the tick order: activations -> jump execution -> mode transitions
    // -> attribute resolve -> per-mode locomotion -> effect aging. Physics runs
    // later in its own phase, consuming DesiredVelocity and PendingJumpSpeed.
    struct GameplayRunnerSystem
    {
        explicit GameplayRunnerSystem(Registry*& registry)
            : RegistryInstance(registry)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;
            World& world = RegistryInstance->Components;
            if (!world.IsRegistered<MovementState>())
                return;

            const float dt = static_cast<float>(ctx.Time.DeltaSeconds);
            ProcessAbilityActivations(world);
            TickJumpExecution(world);
            TickLocomotionTransitions(world, dt);
            ResolveAttributesWithEffects(world);
            TickGroundLocomotion(world, dt);
            TickAirLocomotion(world, dt);
            TickEffects(world, dt);
        }

        Registry*& RegistryInstance;
    };

    // Places the active camera from its rig each frame (first/third/fixed selected
    // by the rig's mode), following the pawn. FrameUpdate so look stays smooth and
    // lands before the extract phase rebuilds world transforms.
    struct CameraFollowSystem
    {
        explicit CameraFollowSystem(Registry*& registry)
            : RegistryInstance(registry)
        {
        }

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;
            World& world = RegistryInstance->Components;
            if (!world.IsRegistered<CameraRig>())
                return;
            const ActiveCameraService* cameraService =
                RegistryInstance->Resources.TryGet<ActiveCameraService>();
            if (cameraService == nullptr || !cameraService->HasActive())
                return;

            const EntityId cameraEntity = cameraService->GetActive();
            CameraRig* rig = world.TryGet<CameraRig>(cameraEntity);
            LocalTransform* cameraTransform = world.TryGet<LocalTransform>(cameraEntity);
            if (rig == nullptr || cameraTransform == nullptr)
                return;

            rig->Yaw -= ctx.Input.MouseDeltaX * rig->Sensitivity;
            rig->Pitch -= ctx.Input.MouseDeltaY * rig->Sensitivity;
            rig->Pitch = std::clamp(rig->Pitch, rig->MinPitch, rig->MaxPitch);

            Vec3d targetPosition = Vec3d::Zero();
            if (const WorldTransform* target = world.TryGet<WorldTransform>(rig->Target))
                targetPosition = target->Value.Position;

            const CameraPose pose = ComputeCameraPose(*rig, targetPosition);
            if (!pose.Override)
                return;
            cameraTransform->Value.Position = pose.Position;
            cameraTransform->Value.Rotation = pose.Rotation;
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

    // The +map mechanism: the startup script runs `map <name>` immediately after
    // this hook (ConsolePhase::GameLoaded), landing in LoadMap.
    engine.Console().SetMapHandler(
        [this](std::string_view mapName) { return LoadMap(mapName); });

    std::printf("Sencha game template\n");
    std::printf("  Load a map: +map levels/<name> (cooked under assets/.cooked/)\n");
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
            InitializeDefault3DRegistry(registry, meshes, materialSets);
            // Physics components must be registered before any entity is created
            // in this zone's World (build runs before finalize spawns entities).
            // The helper also registers the runtime link components the bridges
            // add at reconcile time, so they cannot be forgotten.
            RegisterPhysicsComponents(registry.Components);
            // Movement and camera components plus the movement.* tag hierarchy,
            // registered here for the same reason: storage must exist before the
            // finalize pass spawns the pawn and camera entities.
            InitializeMovementRegistry(registry.Components);
            if (!registry.Components.IsRegistered<CameraRig>())
                registry.Components.RegisterComponent<CameraRig>();
            *parsed = ParseSceneFile(scenePath);
        },
        [this, parsed, &logging, collisionSidecar](Registry& registry) {
            Logger& finalizeLog = logging.GetLogger<TemplateGame>();
            if (!parsed->Json)
            {
                finalizeLog.Error("TemplateGame: {}", parsed->Error);
                return;
            }

            SceneLoadError loadError;
            SceneSerializationContext sceneContext(logging, &RuntimeAssetState().Assets);
            if (!LoadSceneJson(*parsed->Json, registry, sceneContext, &loadError))
            {
                finalizeLog.Error("TemplateGame: scene load error: {}", loadError.Message);
                return;
            }

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

            // Load the level's cooked brush collision: spawns the static colliders
            // the character walks on. No collision authoring; it rode the cook.
            if (PhysicsShapes)
                LoadZoneCollision(registry.Components, *PhysicsShapes, collisionSidecar,
                                  std::string(kCookedScanRoot));

            ActiveZoneRegistry = &registry;
            ZoneActive = true;
        },
        ZoneParticipation{ .Visible = true, .Physics = true, .Logic = true, .Audio = true },
        std::move(preload));

    ConsoleResult result;
    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

void TemplateGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    RegisterPhysics(ctx.Schedule);
    // The physics step system owns the shared collision cache; grab it so map
    // load can fill it with the level's cooked brush collision.
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        PhysicsShapes = &step->GetShapeCache();
    ctx.Schedule.Register<CharacterInputSystem>(ActiveZoneRegistry);
    ctx.Schedule.Register<GameplayRunnerSystem>(ActiveZoneRegistry);
    ctx.Schedule.After<GameplayRunnerSystem, CharacterInputSystem>();
    ctx.Schedule.Register<CameraFollowSystem>(ActiveZoneRegistry);
    ctx.Schedule.Register<SpinSystem>(ActiveZoneRegistry);
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
