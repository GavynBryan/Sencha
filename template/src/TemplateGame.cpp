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
#include <graphics/vulkan/GraphicsServices.h>
#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <render/Camera.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/DefaultZoneBuilder.h>

#include <SDL3/SDL.h>

#include <cassert>
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

    struct FreeCameraLookSystem
    {
        FreeCameraLookSystem(Registry*& registry, FreeCamera& freeCamera)
            : RegistryInstance(registry)
            , FreeCam(freeCamera)
        {
        }

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;
            FreeCam.UpdateLook(ctx.Input);
            FreeCam.ApplyRotation(RegistryInstance->Components);
        }

        Registry*& RegistryInstance;
        FreeCamera& FreeCam;
    };

    struct FreeCameraMovementSystem
    {
        FreeCameraMovementSystem(Registry*& registry, FreeCamera& freeCamera)
            : RegistryInstance(registry)
            , FreeCam(freeCamera)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;
            FreeCam.TickFixed(
                ctx.Input, RegistryInstance->Components, static_cast<float>(ctx.Time.DeltaSeconds));
        }

        Registry*& RegistryInstance;
        FreeCamera& FreeCam;
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

void TemplateGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(logging, graphics.Buffers, graphics.Images, graphics.Descriptors, graphics.Samplers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // Mount: authored assets, then the cooked overlay (cooked wins).
    ScanAssetsDirectory(std::string(kAuthoredRoot), runtimeAssets.Registry);
    ScanAssetsDirectory(std::string(kCookedScanRoot), runtimeAssets.Registry);

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
    std::printf("  Right mouse: look | WASD: move | Q/E: down/up | Shift: fast | Escape: quit\n");
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
            *parsed = ParseSceneFile(scenePath);
        },
        [this, parsed, &logging](Registry& registry) {
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
            // geometry, so spawn a fly-cam to make it viewable.
            EntityId camera = FindFirstCamera(registry);
            if (!camera.IsValid())
            {
                Transform3f start;
                start.Position = Vec3d{ 0.0f, 3.0f, 10.0f };
                camera = CreateDefaultEntity(registry, start);
                AddDefaultCamera(registry, camera, CameraComponent{}, /*makeActive*/ true);
            }
            else
            {
                registry.Resources.Get<ActiveCameraService>().SetActive(camera);
            }

            FreeCam = FreeCamera{};
            FreeCam.Entity = camera;
            ActiveZoneRegistry = &registry;
            ZoneActive = true;
        },
        ZoneParticipation{ .Visible = true, .Logic = true, .Audio = true },
        std::move(preload));

    ConsoleResult result;
    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

void TemplateGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    ctx.Schedule.Register<FreeCameraLookSystem>(ActiveZoneRegistry, FreeCam);
    ctx.Schedule.Register<FreeCameraMovementSystem>(ActiveZoneRegistry, FreeCam);
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
