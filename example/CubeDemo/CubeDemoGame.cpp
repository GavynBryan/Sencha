#include "CubeDemoGame.h"

#include "CubeDemoSystems.h"

#include <app/DefaultRenderPipeline.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/CaptionRuntime.h>
#include <core/assets/AssetManifest.h>
#include <world/transform/TransformComponents.h>
#include <app/Engine.h>
#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AudioCook.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/BlendCook.h>
#include <assets/cook/MeshCook.h>
#include <assets/cook/TextureCook.h>
#endif
#include <world/serialization/SceneSerializer.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <zone/DefaultZoneBuilder.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/IDebugPanel.h>
#include <debug/ImGuiDebugOverlay.h>
#include <debug/TimingPanel.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <imgui.h>
#endif

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>

namespace
{
#ifdef SENCHA_ENABLE_DEBUG_UI
    class CubeDemoPanel : public IDebugPanel
    {
    public:
        CubeDemoPanel(RenderQueue& queue,
                      CameraRenderData& camera,
                      FreeCamera& freeCamera,
                      Registry*& registry,
                      DemoScene& scene)
            : Queue(queue)
            , Camera(camera)
            , FreeCam(freeCamera)
            , RegistryInstance(registry)
            , Scene(scene)
        {
        }

        void Draw() override
        {
            ImGui::Begin("Cube Demo");
            if (RegistryInstance == nullptr)
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
            ImGui::DragFloat("Move speed", &FreeCam.MoveSpeed, 0.1f, 0.1f, 50.0f);
            ImGui::DragFloat("Mouse sensitivity", &FreeCam.MouseSensitivity, 0.0001f, 0.0001f, 0.02f);

            if (LocalTransform* cube =
                    RegistryInstance->Components.TryGet<LocalTransform>(Scene.CenterCube))
                ImGui::DragFloat3("Center cube position", &cube->Value.Position.X, 0.05f);

            ImGui::End();
        }

    private:
        RenderQueue& Queue;
        CameraRenderData& Camera;
        FreeCamera& FreeCam;
        Registry*& RegistryInstance;
        DemoScene& Scene;
    };
#endif
}

void CubeDemoGame::OnStart(GameStartupContext& ctx)
{
    Engine& engine = ctx.EngineInstance;
    ServiceHost& services = engine.Services();
    LoggingProvider& logging = services.GetLoggingProvider();
    DebugService& debug = services.Get<DebugService>();
    DebugLogSink& debugLog = debug.GetLogSink();
    auto& buffers = services.Get<VulkanBufferService>();
    auto& images = services.Get<VulkanImageService>();
    auto& descriptors = services.Get<VulkanDescriptorCache>();
    auto& samplers = services.Get<VulkanSamplerCache>();

    Assets.emplace(logging, buffers, images, descriptors, samplers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    InitSceneSerializer();

#ifdef SENCHA_ENABLE_COOK
    // Import-on-demand (docs/assets/pipeline.md, Decision B): cook source
    // formats (the checker PNG → usage-tagged .stex, the torus .glb →
    // tangent-carrying .smesh) into .cooked/ and register the artifacts
    // under their virtual paths. Runs before the scan; the scanner only
    // knows runtime formats and skips .cooked/. Per-source failures are
    // logged and isolated by the driver; the demo proceeds with whatever
    // cooked.
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
        (void)ImportAssetsOnDemand("assets", importers, runtimeAssets.Registry, logging);
    }
#endif

    ScanAssetsDirectory("assets", runtimeAssets.Registry);

    // Async zone load (docs/ecs/parallelization.md): file IO, JSON parse, and
    // the registry skeleton happen on the task thread; deserialization and
    // game-state wiring happen in finalize, on the main thread, in the same
    // commit that attaches the zone. DemoRegistry flips from null there —
    // systems and the panel idle until it does.
    ZoneLoader.emplace(engine.Tasks(), engine.Zones(), engine.Runtime());
    Preloader.emplace(logging, runtimeAssets.Registry, runtimeAssets.Assets, engine.Tasks());

    // Manifest-driven preload (docs/assets/pipeline.md, Decision D): the
    // zone's assets stream through the async lane and the attach waits for
    // them. A missing manifest is the sync fallback — slower first frame,
    // same result.
    std::shared_ptr<AssetPreload> preload;
    AssetManifest manifest;
    std::string manifestError;
    if (LoadAssetManifestFile("cube_demo_scene.manifest.json", manifest, &manifestError))
    {
        preload = Preloader->Begin(manifest.Paths);
    }
    else
    {
        logging.GetLogger<CubeDemoGame>().Warn(
            "CubeDemo: no asset manifest ({}); falling back to resolve-on-attach", manifestError);
    }

    auto parsed = std::make_shared<DemoSceneParse>();
    StaticMeshCache* meshes = &runtimeAssets.StaticMeshes;
    MaterialCache* materials = &runtimeAssets.Materials;
    AudioClipCache* audioClips = &runtimeAssets.AudioClips;
    AudioService* audio = services.TryGet<AudioService>();
    CaptionRuntime* captions = services.TryGet<CaptionRuntime>();
    if (captions != nullptr)
    {
        CaptionSettings captionSettings;
        captionSettings.ClosedCaptionsEnabled = true;
        captions->SetSettings(captionSettings);
    }

    ZoneLoader->BeginLoad(
        ZoneId{ 1 },
        [parsed, meshes, materials, audioClips, audio, captions](Registry& registry) {
            InitializeDefault3DRegistry(registry, meshes, materials, audioClips, audio, captions);
            *parsed = ParseDemoSceneFile("cube_demo_scene.json");
        },
        [this, parsed, &logging](Registry& registry) {
            if (FinalizeDemoScene(Demo, registry, *parsed,
                                  RuntimeAssetState().Assets, logging, FreeCam))
            {
                DemoRegistry = &registry;
            }
        },
        ZoneParticipation{ .Visible = true, .Logic = true, .Audio = true },
        std::move(preload));

    DefaultRenderPipeline* pipeline = engine.GetRenderPipeline();
    if (pipeline != nullptr)
    {
        pipeline->SetAssetStores(runtimeAssets.StaticMeshes, runtimeAssets.Materials);
        pipeline->AddMeshRenderFeature(services);
    }

#ifdef SENCHA_ENABLE_DEBUG_UI
    auto& windows = services.Get<SdlWindowService>();
    SdlWindow* window = windows.GetPrimaryWindow();
    auto& instance = services.Get<VulkanInstanceService>();
    auto& frames = services.Get<VulkanFrameService>();

    auto debugOverlay =
        std::make_unique<ImGuiDebugOverlay>(debug, *window, instance, frames);
    debugOverlay->AddPanel<ConsolePanel>(debugLog);
    debugOverlay->AddPanel<TimingPanel>(engine.Timing());
    if (pipeline != nullptr)
    {
        debugOverlay->AddPanel<CubeDemoPanel>(
            pipeline->GetRenderQueue(), pipeline->GetCameraData(), FreeCam, DemoRegistry, Demo);
    }
    DebugOverlay = debugOverlay.get();
    auto& renderer = services.Get<Renderer>();
    renderer.AddFeature(std::move(debugOverlay));
#else
    (void)debugLog;
    debug.Open();
#endif

    std::printf("Sencha Cube Demo\n");
    std::printf("  Right mouse + move: look\n");
    std::printf("  WASD: move, Q/E: down/up, Shift: fast\n");
    std::printf("  F1: pause simulation (timescale 0)\n");
    std::printf("  `: debugger when built with SENCHA_ENABLE_DEBUG_UI=ON\n");
    std::printf("  Escape: quit\n");
}

void CubeDemoGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    RegisterCubeDemoSystems(ctx.Schedule, DemoRegistry, FreeCam, Demo);
}

void CubeDemoGame::OnPlatformEvent(PlatformEventContext& ctx)
{
#ifdef SENCHA_ENABLE_DEBUG_UI
    if (DebugOverlay != nullptr && DebugOverlay->ProcessSdlEvent(ctx.Event))
        ctx.Handled = true;
#else
    (void)ctx;
#endif
    if (ctx.Handled)
        return;

    if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(ctx.EngineInstance, true);
    }
    else if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(ctx.EngineInstance, false);
    }
    else if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        SetRelativeMouseMode(ctx.EngineInstance, false);
    }
}

void CubeDemoGame::OnShutdown(GameShutdownContext& ctx)
{
#ifdef SENCHA_ENABLE_DEBUG_UI
    DebugOverlay = nullptr;
#endif
    SetRelativeMouseMode(ctx.EngineInstance, false);

    // Best-effort cancel if the load is still in flight; if the build is
    // mid-run on the task thread, Engine::Shutdown drops the undrained
    // commit, so the zone never attaches either way.
    if (ZoneLoader && ZoneLoader->IsLoading(ZoneId{ 1 }))
        ZoneLoader->CancelLoad(ZoneId{ 1 });
    ZoneLoader.reset();

    ctx.EngineInstance.Zones().DestroyZone(ZoneId{ 1 });
    DemoRegistry = nullptr;
}

RuntimeAssets& CubeDemoGame::RuntimeAssetState()
{
    assert(Assets.has_value() && "RuntimeAssets must be constructed before use");
    return *Assets;
}

const RuntimeAssets& CubeDemoGame::RuntimeAssetState() const
{
    assert(Assets.has_value() && "RuntimeAssets must be constructed before use");
    return *Assets;
}

void CubeDemoGame::SetRelativeMouseMode(Engine& engine, bool enabled)
{
    SdlWindow* window = engine.Services().Get<SdlWindowService>().GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;

    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) == enabled)
        return;

    SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);
}
