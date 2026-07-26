#include "SceneViewerGame.h"

#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/GameModule.h>
#include <audio/AudioSourceComponent.h>
#include <camera/CameraRegistration.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <render/ProbeVolumeSet.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneLoadPackage.h>
#include <zone/ZonePackageSceneLoader.h>

#include <SDL3/SDL.h>

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
#include <variant>

namespace
{
constexpr std::string_view kAuthoredRoot = "assets";
constexpr std::string_view kCookedScanRoot = "assets/.cooked";
constexpr ZoneId kPlayZone{ 1 };

struct SceneBuildResult
{
    bool Success = false;
    std::string Error;
};

void BuildScenePackage(
    ZoneLoadPackage& package,
    SceneBuildResult& result,
    const std::string& path,
    const ComponentSerializerRegistry& serializers)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        result.Error = "could not open scene file '" + path + "'";
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    JsonParseError parseError;
    const std::optional<JsonValue> json =
        JsonParse(buffer.str(), &parseError);
    if (!json)
    {
        result.Error = "scene JSON parse error at "
            + std::to_string(parseError.Position)
            + ": " + parseError.Message;
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

EntityId CreateViewerCamera(World& world)
{
    Transform3f transform;
    transform.Position = Vec3d{ 0.0f, 3.0f, 10.0f };

    const EntityId camera =
        world.CreateEntity(PersistentStoragePartition);
    world.AddComponent<LocalTransform>(
        camera,
        LocalTransform{ transform });
    world.AddComponent<WorldTransform>(
        camera,
        WorldTransform{ transform });
    world.AddComponent<CameraComponent>(
        camera,
        CameraComponent{});
    world.GetResource<ActiveCameraService>().SetActive(camera);
    return camera;
}

struct FreeCameraLookSystem
{
    explicit FreeCameraLookSystem(FreeCamera& camera)
        : Camera(camera)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        Camera.UpdateLook(ctx.Input);
        Camera.ApplyRotation(ctx.Entities);
    }

    FreeCamera& Camera;
};

struct FreeCameraMovementSystem
{
    explicit FreeCameraMovementSystem(FreeCamera& camera)
        : Camera(camera)
    {
    }

    void FixedLogic(FixedLogicContext& ctx)
    {
        Camera.TickFixed(
            ctx.Input,
            ctx.Entities,
            static_cast<float>(ctx.Time.DeltaSeconds));
    }

    FreeCamera& Camera;
};

// Drives the active camera along a fixed orbit as a pure function of an
// internal frame counter, so every run of a given build renders the exact
// same view sequence. Runs in FrameUpdate (after the fixed-tick free-cam),
// so it wins for the presented frame whenever armed. Off by default; free
// fly-cam is untouched unless sceneviewer.camera.scripted is set.
struct ScriptedCameraPathSystem
{
    ScriptedCameraPathSystem(FreeCamera& freeCamera, const bool& enabled)
        : FreeCam(freeCamera)
        , Enabled(enabled)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Enabled)
            return;
        LocalTransform* transform =
            ctx.Entities.TryGet<LocalTransform>(FreeCam.Entity);
        if (transform == nullptr)
            return;

        const double angle = static_cast<double>(FrameCounter) * kAngularStep;
        transform->Value.Position = Vec3d{
            static_cast<float>(std::cos(angle) * kRadius), kHeight,
            static_cast<float>(std::sin(angle) * kRadius) };
        const float yaw = static_cast<float>(angle) + kPi; // face the orbit center
        transform->Value.Rotation =
            Quatf::FromAxisAngle(Vec3d::Up(), yaw)
            * Quatf::FromAxisAngle(Vec3d::Right(), kPitch);
        ++FrameCounter;
    }

    FreeCamera& FreeCam;
    const bool& Enabled;
    std::uint64_t FrameCounter = 0;

    static constexpr double kAngularStep = 0.012;
    static constexpr double kRadius = 8.0;
    static constexpr double kHeight = 3.0;
    static constexpr float kPitch = -0.35f;
    static constexpr float kPi = 3.14159265358979323846f;
};
} // namespace

void SceneViewerGame::OnRegisterComponents(
    ComponentSerializerRegistry&)
{
    InitSceneSerializer();
}

void SceneViewerGame::OnStart(GameStartupContext&)
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

    // Mount: authored assets, then the cooked overlay (cooked wins), then the
    // cooked index. The index adds artifacts the physical scan cannot key,
    // notably cooked textures (asset://...png serving cooked .stex bytes);
    // without it a material's texture refs fall back to the neutral default.
    ScanAssetsDirectory(
        std::string(kAuthoredRoot),
        runtimeAssets.Registry);
    ScanAssetsDirectory(
        std::string(kCookedScanRoot),
        runtimeAssets.Registry);
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
        logging.GetLogger<SceneViewerGame>().Warn(
            "SceneViewer: no asset id map ({}); refs resolve by path only",
            idMapError);
    }

    World& world = engine.World().Entities();
    world.AddResource<StaticMeshComponentAssets>(
        &runtimeAssets.StaticMeshes,
        &runtimeAssets.MaterialSets);
    world.AddResource<ZoneLightmapComponentAssets>(
        &runtimeAssets.Textures);
    world.AddResource<AudioSourceRuntime>(
        &runtimeAssets.AudioClips,
        &engine.Audio(),
        &engine.Captions());
    RegisterCameraComponents(world);

    SceneContext = std::make_unique<SceneSerializationContext>(
        logging,
        &runtimeAssets.Assets);
    ZoneLoader.emplace(
        engine.Tasks(),
        engine.World(),
        engine.RuntimeComponents(),
        DefaultComponentSerializerRegistry(),
        *SceneContext,
        engine.Runtime());
    Preloader.emplace(
        logging,
        runtimeAssets.Registry,
        runtimeAssets.Assets,
        engine.Tasks());

    CameraEntity = CreateViewerCamera(world);
    FreeCam = FreeCamera{};
    FreeCam.Entity = CameraEntity;

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

    engine.Console().Registry().RegisterCVar({
        .Name = "sceneviewer.camera.scripted",
        .Owner = "sceneviewer",
        .Type = CVarType::Bool,
        .DefaultValue = false,
        .CurrentValue = false,
        .Flags = CVarFlags::Transient,
        .Help = "Drive the camera along a fixed deterministic orbit instead of "
                "free-fly, so a run renders an identical view sequence.",
        .Source = { "sceneviewer" },
        .OnChange = [this](const CVarChangeContext& ctx) {
            ScriptedCameraEnabled = std::get<bool>(ctx.NewValue);
        },
    });

    std::printf("Sencha scene viewer\n");
    std::printf("  Load a map: +map levels/<name> (cooked under assets/.cooked/)\n");
    std::printf("  Right mouse: look | WASD: move | Q/E: down/up\n");
}

ConsoleResult SceneViewerGame::LoadMap(
    std::string_view mapName)
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
        logging.GetLogger<SceneViewerGame>().Warn(
            "SceneViewer: no manifest for '{}' ({}); resolve-on-import",
            std::string(mapName),
            manifestError);
    }

    auto buildResult = std::make_shared<SceneBuildResult>();
    auto probes = std::make_shared<ProbeVolumeFile>();
    const ComponentSerializerRegistry* serializers =
        &DefaultComponentSerializerRegistry();
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
        [this, buildResult, probes, &logging](
            RuntimeWorld&,
            RuntimeZoneRecord& zone)
        {
            if (!buildResult->Success)
            {
                logging.GetLogger<SceneViewerGame>().Error(
                    "SceneViewer: scene load error: {}",
                    buildResult->Error);
                return false;
            }
            if (DefaultRenderPipeline* pipeline =
                    GetEngine().GetRenderPipeline())
            {
                AttachZoneProbes(
                    pipeline->GetProbeVolumes(), zone, *probes);
            }
            ZoneActive = true;
            return true;
        },
        ZoneParticipation{
            .Visible = true,
            .Logic = true,
            .Audio = true,
        },
        std::move(preload));

    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

void SceneViewerGame::OnRegisterSystems(
    SystemRegisterContext& ctx)
{
    ctx.Schedule.Register<FreeCameraLookSystem>(FreeCam);
    ctx.Schedule.Register<FreeCameraMovementSystem>(FreeCam);
    ctx.Schedule.Register<ScriptedCameraPathSystem>(
        FreeCam, ScriptedCameraEnabled);
}

void SceneViewerGame::OnPlatformEvent(
    PlatformEventContext& ctx)
{
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

void SceneViewerGame::OnShutdown(GameShutdownContext&)
{
    SetRelativeMouseMode(false);

    Engine& engine = GetEngine();
    RuntimeWorld& runtime = engine.World();
    if (ZoneLoader && ZoneLoader->IsLoading(kPlayZone))
        (void)ZoneLoader->CancelLoad(kPlayZone);

    if (runtime.FindZone(kPlayZone) != nullptr)
    {
        (void)runtime.RequestDetach(kPlayZone);
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
    }

    if (runtime.Entities().IsAlive(CameraEntity))
        runtime.Entities().DestroyEntity(CameraEntity);
    runtime.Entities()
        .GetResource<ActiveCameraService>()
        .SetActive(EntityId{});

    if (StaticMeshComponentAssets* meshAssets =
            runtime.Entities().TryGetResource<StaticMeshComponentAssets>())
    {
        meshAssets->Meshes = nullptr;
        meshAssets->MaterialSets = nullptr;
    }
    if (ZoneLightmapComponentAssets* lightmapAssets =
            runtime.Entities().TryGetResource<ZoneLightmapComponentAssets>())
    {
        lightmapAssets->Textures = nullptr;
    }
    if (AudioSourceRuntime* audioRuntime =
            runtime.Entities().TryGetResource<AudioSourceRuntime>())
    {
        audioRuntime->Clips = nullptr;
        audioRuntime->Audio = nullptr;
        audioRuntime->Captions = nullptr;
    }

    CameraEntity = EntityId{};
    ZoneActive = false;
    ZoneLoader.reset();
    SceneContext.reset();

    // Release the GPU-backed asset caches while OnShutdown still runs with the
    // engine (device, allocators, descriptor pools) up. Zone detach above
    // returned the zone's mesh and texture handles to these caches; freeing
    // them now, rather than at the module-static Game's own destruction (which
    // runs at process exit after the device is gone), is what keeps a clean
    // window close from freeing GPU handles into dead graphics services.
    Preloader.reset();
    Assets.reset();
}

RuntimeAssets& SceneViewerGame::RuntimeAssetState()
{
    assert(Assets.has_value()
           && "RuntimeAssets must be constructed before use");
    return *Assets;
}

void SceneViewerGame::SetRelativeMouseMode(bool enabled)
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
    static SceneViewerGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
