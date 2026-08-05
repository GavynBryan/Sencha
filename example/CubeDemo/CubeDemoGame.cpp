#include "CubeDemoGame.h"

#include <input/InputActionResolveSystem.h>
#include <input/InputBindingCache.h>
#include <input/InputRegistration.h>

#include "CubeDemoSystems.h"

#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/CaptionRuntime.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/logging/LoggingProvider.h>
#include <core/console/ConsoleService.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <render/StaticMeshComponent.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <zone/ZoneLoadPackage.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AudioCook.h>
#include <assets/cook/BlendCook.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/MeshCook.h>
#include <assets/cook/TextureCook.h>
#endif

#include <debug/IDebugPanel.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

namespace
{
constexpr ZoneId kDemoZone{ 1 };

class CubeDemoPanel : public IDebugPanel
{
public:
    CubeDemoPanel(
        RenderQueue& queue,
        CameraRenderData& camera,
        FreeCamera& freeCamera,
        World& world,
        DemoScene& scene)
        : Queue(queue)
        , Camera(camera)
        , FreeCam(freeCamera)
        , Entities(world)
        , Scene(scene)
    {
    }

    void Draw() override
    {
        ImGui::Begin("Cube Demo");
        if (!Entities.IsAlive(Scene.CenterCube))
        {
            ImGui::Text("Zone loading...");
            ImGui::End();
            return;
        }

        ImGui::Text("Right mouse: look");
        ImGui::Text("WASD: move, Q/E: down/up, Shift: fast");
        ImGui::Separator();
        ImGui::Text("Opaque queue: %zu", Queue.Opaque().size());
        ImGui::Text("Camera entity: %u", Camera.Entity.Index);
        ImGui::DragFloat(
            "Move speed",
            &FreeCam.MoveSpeed,
            0.1f,
            0.1f,
            50.0f);
        ImGui::DragFloat(
            "Mouse sensitivity",
            &FreeCam.MouseSensitivity,
            0.0001f,
            0.0001f,
            0.02f);

        if (LocalTransform* cube =
                Entities.TryGet<LocalTransform>(Scene.CenterCube))
        {
            ImGui::DragFloat3(
                "Center cube position",
                &cube->Value.Position.X,
                0.05f);
        }

        ImGui::End();
    }

private:
    RenderQueue& Queue;
    CameraRenderData& Camera;
    FreeCamera& FreeCam;
    World& Entities;
    DemoScene& Scene;
};

#ifdef SENCHA_ENABLE_COOK
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

uint32_t ReadAutoExitFrameCount()
{
    const char* raw =
        std::getenv("SENCHA_CUBE_DEMO_EXIT_AFTER_FRAMES");
    if (raw == nullptr || raw[0] == '\0')
        return 0;

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed == 0
        || parsed > std::numeric_limits<uint32_t>::max())
    {
        return 0;
    }
    return static_cast<uint32_t>(parsed);
}

struct AutoExitSystem
{
    AutoExitSystem(Engine& engine, uint32_t frameCount)
        : EngineInstance(engine)
        , RemainingFrames(frameCount)
    {
    }

    void EndFrame(EndFrameContext&)
    {
        if (RemainingFrames == 0)
            return;
        --RemainingFrames;
        if (RemainingFrames == 0)
            EngineInstance.RequestExit();
    }

    Engine& EngineInstance;
    uint32_t RemainingFrames = 0;
};
} // namespace

void CubeDemoGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    DebugService& debug = engine.Debug();
    DebugLogSink& debugLog = debug.GetLogSink();
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(
        logging,
        graphics.Buffers,
        graphics.Images,
        graphics.Descriptors,
        graphics.Samplers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // The demo's own controls. Registered as a procedural profile so the camera
    // works without shipping input assets in the demo's generated content.
    {
        World& world = engine.World().Entities();
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
            FreeCam.Actions.DumpTrace = actions->Find("fly_dump_trace");
            FlyInput = world.GetResource<InputContextSet>().Activate("fly");
        }
        else
        {
            logging.GetLogger<CubeDemoGame>().Error(
                "fly camera input did not bind: {}",
                DescribeBindErrors(bindings.Status(profile)));
        }
    }

#ifdef SENCHA_ENABLE_COOK
    {
        PngTextureImporter pngImporter;
        GltfMeshImporter gltfImporter;
        BlendMeshImporter blendImporter;
        AudioClipImporter audioImporter;
        AssetImporterRegistry importers;
        importers.Register(pngImporter);
        importers.Register(gltfImporter);
        importers.Register(blendImporter);
        importers.Register(audioImporter);
        (void)ImportAssetsOnDemand(
            "assets",
            importers,
            runtimeAssets.Registry,
            logging);
    }
#endif

    ScanAssetsDirectory("assets", runtimeAssets.Registry, runtimeAssets.Assets.Kinds());

    AssetIdMap idMap;
    std::string idMapError;
    const std::string idMapPath =
        std::string("assets/")
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
        logging.GetLogger<CubeDemoGame>().Warn(
            "CubeDemo: no asset id map ({}); refs resolve by path only",
            idMapError);
    }

#ifdef SENCHA_ENABLE_COOK
    HotReloadImporters.Register(HotReloadPngImporter);
    HotReloadImporters.Register(HotReloadGltfImporter);
    HotReloadImporters.Register(HotReloadBlendImporter);
    Reloader.emplace(
        logging,
        runtimeAssets.Assets,
        runtimeAssets.Registry,
        HotReloadImporters,
        engine.Tasks(),
        std::string("assets"));
    Watcher.emplace(
        logging,
        std::string("assets"),
        std::vector<std::string>{
            ".png", ".glb", ".gltf", ".blend", ".smat",
        });
    Watcher->Initialize();
#endif

    World& world = engine.World().Entities();
    world.AddResource<StaticMeshComponentAssets>(
        &runtimeAssets.StaticMeshes,
        &runtimeAssets.MaterialSets);
    world.AddResource<AudioSourceRuntime>(
        &runtimeAssets.AudioClips,
        &engine.Audio(),
        &engine.Captions());

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

    std::shared_ptr<AssetPreload> preload;
    AssetManifest manifest;
    std::string manifestError;
    if (LoadAssetManifestFile(
            "cube_demo_scene.manifest.json",
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
        logging.GetLogger<CubeDemoGame>().Warn(
            "CubeDemo: no asset manifest ({}); resolve-on-import",
            manifestError);
    }

    CaptionRuntime* captions = &engine.Captions();
    CaptionSettings captionSettings;
    captionSettings.ClosedCaptionsEnabled = true;
    captions->SetSettings(captionSettings);

    auto parsed = std::make_shared<DemoSceneParse>();
    auto packageBuilt = std::make_shared<bool>(false);
    auto packageError = std::make_shared<std::string>();
    const ComponentSerializerRegistry* serializers = &engine.SceneSerializers();
    ZoneLoader->BeginLoad(
        kDemoZone,
        [parsed, packageBuilt, packageError, serializers](
            ZoneLoadPackage& package)
        {
            *parsed = ParseDemoSceneFile(
                "cube_demo_scene.cooked.json");
            if (!parsed->Json)
                *parsed = ParseDemoSceneFile("cube_demo_scene.json");

            SceneLoadError loadError;
            *packageBuilt = BuildDemoScenePackage(
                package,
                *parsed,
                *serializers,
                &loadError);
            if (!*packageBuilt)
                *packageError = loadError.Message;
        },
        [this,
         parsed,
         packageBuilt,
         packageError,
         &logging](
            RuntimeWorld& runtime,
            RuntimeZoneRecord& zone)
        {
            if (!*packageBuilt)
            {
                logging.GetLogger<CubeDemoGame>().Error(
                    "CubeDemo: scene package error: {}",
                    *packageError);
                return false;
            }

            if (!FinalizeDemoScene(
                    Demo,
                    runtime,
                    zone,
                    *parsed,
                    logging,
                    FreeCam))
            {
                return false;
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

    DefaultRenderPipeline* pipeline =
        engine.GetRenderPipeline();
    if (pipeline != nullptr)
    {
        pipeline->SetAssetStores(
            runtimeAssets.StaticMeshes,
            runtimeAssets.Materials,
            runtimeAssets.MaterialSets,
            &runtimeAssets.Textures);
        pipeline->AddMeshRenderFeature(graphics);
    }

#ifdef SENCHA_ENABLE_DEBUG_UI
    // The engine creates the overlay itself (console + timing + render
    // stats); only the demo's own panel is added, queued until the overlay
    // exists.
    (void)debugLog;
    if (pipeline != nullptr)
    {
        engine.AddDebugPanel(std::make_unique<CubeDemoPanel>(
            pipeline->GetRenderQueue(),
            pipeline->GetCameraData(),
            FreeCam,
            world,
            Demo));
    }
#else
    (void)debugLog;
    debug.Open();
#endif

    std::printf("Sencha Cube Demo\n");
    std::printf("  Right mouse + move: look\n");
    std::printf("  WASD: move, Q/E: down/up, Shift: fast\n");
    std::printf("  F1: pause simulation\n");
    std::printf("  Escape: quit\n");
}

void CubeDemoGame::OnRegisterSystems(
    SystemRegisterContext& ctx)
{
    RegisterInputSystems(
        ctx.Schedule,
        RuntimeAssetState().DataAssets,
        GetEngine().Logging());
    RegisterCubeDemoSystems(
        ctx.Schedule,
        FreeCam,
        Demo,
        &GetEngine().Captions());
#ifdef SENCHA_ENABLE_COOK
    ctx.Schedule.Register<HotReloadPollSystem>(Watcher, Reloader);
#endif
    if (const uint32_t autoExitFrames =
            ReadAutoExitFrameCount();
        autoExitFrames > 0)
    {
        ctx.Schedule.Register<AutoExitSystem>(
            GetEngine(),
            autoExitFrames);
    }
}

void CubeDemoGame::OnPlatformEvent(
    PlatformEventContext& ctx)
{
#ifndef SENCHA_ENABLE_DEBUG_UI
    (void)ctx;
#endif
    if (ctx.Handled)
        return;

    if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(GetEngine(), true);
    }
    else if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
             && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(GetEngine(), false);
    }
    else if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        SetRelativeMouseMode(GetEngine(), false);
    }
}

void CubeDemoGame::OnShutdown(GameShutdownContext&)
{
    SetRelativeMouseMode(GetEngine(), false);

    Engine& engine = GetEngine();
    RuntimeWorld& runtime = engine.World();
    if (ZoneLoader && ZoneLoader->IsLoading(kDemoZone))
        (void)ZoneLoader->CancelLoad(kDemoZone);

    if (runtime.FindZone(kDemoZone) != nullptr)
    {
        (void)runtime.RequestDetach(kDemoZone);
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

    Demo = DemoScene{};
    FreeCam = FreeCamera{};
    ZoneActive = false;
    ZoneLoader.reset();
    SceneContext.reset();

    // Release the GPU-backed asset caches while OnShutdown still runs with the
    // engine (device, allocators, descriptor pools) up. Zone detach above
    // returned the zone's mesh and texture handles to these caches; the hot
    // reloader and preloader hold references into the asset system, so they
    // drop first. Left to the module-static Game's own destruction (process
    // exit, after the device is gone), the caches would free GPU handles into
    // dead graphics services and corrupt the heap on a clean window close.
    Preloader.reset();
#ifdef SENCHA_ENABLE_COOK
    Reloader.reset();
    Watcher.reset();
#endif
    Assets.reset();
}

RuntimeAssets& CubeDemoGame::RuntimeAssetState()
{
    assert(Assets.has_value()
           && "RuntimeAssets must be constructed before use");
    return *Assets;
}

const RuntimeAssets& CubeDemoGame::RuntimeAssetState() const
{
    assert(Assets.has_value()
           && "RuntimeAssets must be constructed before use");
    return *Assets;
}

void CubeDemoGame::SetRelativeMouseMode(
    Engine& engine,
    bool enabled)
{
    SdlWindow* window =
        engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;
    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) == enabled)
        return;
    SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);
}
