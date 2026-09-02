#include "SceneViewerGame.h"

#include "SceneViewerSystems.h"

#include <anim/AnimationClipPlaybackRuntime.h>
#include <anim/AnimationClipPlaybackSystem.h>
#include <audio/AudioSourceRuntime.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputBindingCache.h>
#include <input/InputRegistration.h>

#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/GameModule.h>
#include <camera/CameraRegistration.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetStoreTable.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <math/geometry/3d/Transform3d.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <render/ProbeVolumeSet.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SmapFormat.h>

#include <SDL3/SDL.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace
{
constexpr std::string_view kAuthoredRoot = "assets";
constexpr std::string_view kCookedScanRoot = "assets/.cooked";
constexpr ZoneId kPlayZone{ 1 };

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

} // namespace

void SceneViewerGame::OnRegisterComponents(ComponentRegistrar&)
{
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
        graphics.Samplers,
        engine.SceneSerializers());
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // Mount: authored assets, then the cooked overlay (cooked wins), then the
    // cooked index. The index adds artifacts the physical scan cannot key,
    // notably cooked textures (asset://...png serving cooked .stex bytes);
    // without it a material's texture refs fall back to the neutral default.
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

    // The viewer's own controls. Registered as a procedural profile so the
    // camera works against any content root, whatever input assets it holds.
    {
        World& world = GetEngine().World().Entities();
        const InputProfileHandle profile =
            RegisterFlyCameraInput(runtimeAssets.DataAssets);
        RegisterInputMapping(world, runtimeAssets.DataAssets, profile);

        InputBindingCache& bindings = world.GetResource<InputBindingCache>();
        if (const InputActionRegistry* actions = bindings.GetActions(profile))
        {
            FreeCam.Actions.Look = actions->Find("fly_look");
            FreeCam.Actions.Move = actions->Find("fly_move");
            FreeCam.Actions.Vertical = actions->Find("fly_vertical");
            FreeCam.Actions.Fast = actions->Find("fly_fast");
            FreeCam.Actions.LookEnable = actions->Find("fly_look_enable");
            FlyInput = world.GetResource<InputContextSet>().Activate("fly");
        }
        else
        {
            logging.GetLogger<SceneViewerGame>().Error(
                "fly camera input did not bind: {}",
                DescribeBindErrors(bindings.Status(profile)));
        }
    }

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
    world.SetResource(runtimeAssets.Assets.Stores());
    world.SetResource(AudioSourceRuntime{
        &runtimeAssets.AudioClips, &engine.Audio(), &engine.Captions() });
    world.SetResource(
        AnimationClipPlaybackRuntime{ &runtimeAssets.AnimationClips });
    RegisterCameraComponents(world);

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

    CameraEntity = CreateViewerCamera(world);
    FreeCam = FreeCamera{};
    FreeCam.Entity = CameraEntity;

    if (DefaultRenderPipeline* pipeline =
            engine.GetRenderPipeline())
    {
        pipeline->SetAssetStores(
            *runtimeAssets.StaticMeshes,
            runtimeAssets.Materials,
            runtimeAssets.MaterialSets,
            runtimeAssets.Textures.get(),
            runtimeAssets.SkinnedMeshes.get(),
            &runtimeAssets.AnimationClips,
            &runtimeAssets.Skeletons);
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

    std::string preloadError;
    std::shared_ptr<AssetPreload> preload =
        Preloader->BeginSceneDependencies(sceneFilePath, &preloadError);
    if (preload == nullptr)
    {
        logging.GetLogger<SceneViewerGame>().Warn(
            "SceneViewer: no preload for '{}' ({}); resolve-on-import",
            std::string(mapName),
            preloadError);
    }

    auto probes = std::make_shared<ProbeVolumeFile>();
    const AsyncTaskHandle load = ZoneLoader->BeginLoadScene(
        kPlayZone,
        sceneAssetPath,
        runtimeAssets.Assets,
        runtimeAssets.Scenes,
        [probes, sceneFilePath](const SmapContents&)
        {
            (void)ReadZoneProbeFile(sceneFilePath, *probes);
        },
        [this, probes](
            RuntimeWorld&,
            RuntimeZoneRecord& zone,
            const SmapContents&)
        {
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
    if (!load.IsValid())
    {
        result.Error("map load refused; see zone load failures");
        return result;
    }

    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

void SceneViewerGame::OnRegisterSystems(
    SystemRegisterContext& ctx)
{
    RegisterInputSystems(
        ctx.Schedule,
        RuntimeAssetState().DataAssets,
        GetEngine().Logging());
    RegisterSceneViewerSystems(ctx.Schedule, FreeCam, ScriptedCameraEnabled);
    // Clip playback: the viewer is where a posed character gets looked at.
    RegisterAnimationSystems(ctx.Schedule);
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

    runtime.Entities().SetResource(AssetStoreTable{});
    runtime.Entities().SetResource(AudioSourceRuntime{});
    runtime.Entities().SetResource(AnimationClipPlaybackRuntime{});

    CameraEntity = EntityId{};
    ZoneActive = false;
    ZoneLoader.reset();
    SceneContext.reset();

    // Before Assets goes: the cache's compiled entries hold Owned handles into
    // RuntimeAssets::DataAssets, and Owned detaches from its owner in its
    // destructor. Left to the World's own teardown the entries would detach
    // from a destroyed cache.
    if (InputBindingCache* bindings =
            runtime.Entities().TryGetResource<InputBindingCache>())
    {
        bindings->Clear();
    }

    // Same reason, one level up: the lease detaches from the InputContextSet
    // resource, and this Game is the module's static instance, destroyed at
    // dlclose long after the World. Dropping it here is what keeps that
    // detach on a live owner.
    FlyInput = InputContextLease{};

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
