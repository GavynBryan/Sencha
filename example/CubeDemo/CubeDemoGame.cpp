#include "CubeDemoGame.h"

#include "CubeDemoSystems.h"

#include <app/DefaultRenderPipeline.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/CaptionRuntime.h>
#include <core/assets/AssetIdMap.h>
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
#include <zone/DefaultZoneBuilder.h>

#include <debug/ConsolePanel.h>
#include <debug/IDebugPanel.h>
#include <debug/ImGuiDebugOverlay.h>
#include <debug/TimingPanel.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
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

#ifdef SENCHA_ENABLE_COOK
    // Throttled per-frame poll of the asset hot-reload watcher (Stage 6a).
    // Detection + re-cook + async stage run here on the main thread; the swap
    // commit lands at the engine's normal async drain point. Holds references
    // to the game's optionals so it tolerates being registered before they
    // populate (the demo's null-check-each-frame idiom).
    struct HotReloadPollSystem
    {
        HotReloadPollSystem(std::optional<AssetSourceWatcher>& watcher,
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
        const char* raw = std::getenv("SENCHA_CUBE_DEMO_EXIT_AFTER_FRAMES");
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
}

void CubeDemoGame::OnStart(GameStartupContext& ctx)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    DebugService& debug = engine.Debug();
    DebugLogSink& debugLog = debug.GetLogSink();
    GraphicsServices& graphics = engine.Graphics();
    auto& buffers = graphics.Buffers;
    auto& images = graphics.Images;
    auto& descriptors = graphics.Descriptors;
    auto& samplers = graphics.Samplers;

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

    // Stable ids (docs/assets/pipeline.md, Decision A / Stage 4e): the
    // cook-maintained id map binds ids to the records import + scan just
    // registered, so id-stamped refs in the cooked scene and manifest
    // resolve by id with the path as fallback. A missing map is the
    // path-only world, which still works.
    {
        AssetIdMap idMap;
        std::string idMapError;
        const std::string idMapPath = std::string("assets/") + std::string(kAssetIdMapFileName);
        if (AssetIdMap::LoadFromFile(idMapPath, idMap, &idMapError))
            ApplyAssetIds(idMap, runtimeAssets.Registry);
        else
            logging.GetLogger<CubeDemoGame>().Warn(
                "CubeDemo: no asset id map ({}); refs resolve by path only", idMapError);
    }

#ifdef SENCHA_ENABLE_COOK
    // Dev-only asset hot reload (Stage 6, Decision H): watch source files and
    // swap the live GPU resource in place on edit — re-cook through the same
    // importers the startup cook uses, decode async, commit-swap at the drain
    // point keeping the handle/slot. Textures keep their bindless index (6a),
    // meshes their buffers (6b); materials reload from authored .smat directly,
    // no importer (6c). The poll itself is a throttled per-frame system
    // (OnRegisterSystems).
    HotReloadImporters.Register(HotReloadPngImporter);
    HotReloadImporters.Register(HotReloadGltfImporter);
    HotReloadImporters.Register(HotReloadBlendImporter);
    Reloader.emplace(logging, runtimeAssets.Assets, runtimeAssets.Registry,
                     HotReloadImporters, engine.Tasks(), std::string("assets"));
    Watcher.emplace(logging, std::string("assets"),
                    std::vector<std::string>{ ".png", ".glb", ".gltf", ".blend", ".smat" });
    Watcher->Initialize();
#endif

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
        const std::vector<std::string> paths =
            ResolveManifestPaths(manifest, runtimeAssets.Registry);
        preload = Preloader->Begin(paths);
    }
    else
    {
        logging.GetLogger<CubeDemoGame>().Warn(
            "CubeDemo: no asset manifest ({}); falling back to resolve-on-attach", manifestError);
    }

    auto parsed = std::make_shared<DemoSceneParse>();
    StaticMeshCache* meshes = &runtimeAssets.StaticMeshes;
    MaterialSetCache* materialSets = &runtimeAssets.MaterialSets;
    AudioClipCache* audioClips = &runtimeAssets.AudioClips;
    AudioService* audio = &engine.Audio();
    CaptionRuntime* captions = &engine.Captions();
    if (captions != nullptr)
    {
        CaptionSettings captionSettings;
        captionSettings.ClosedCaptionsEnabled = true;
        captions->SetSettings(captionSettings);
    }

    ZoneLoader->BeginLoad(
        ZoneId{ 1 },
        [parsed, meshes, materialSets, audioClips, audio, captions](Registry& registry) {
            InitializeDefault3DRegistry(registry, meshes, materialSets, audioClips, audio, captions);
            // Cooked scene first (id-stamped refs, Stage 4e); the authored
            // scene is the fallback when no cook has run.
            *parsed = ParseDemoSceneFile("cube_demo_scene.cooked.json");
            if (!parsed->Json)
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
        pipeline->SetAssetStores(runtimeAssets.StaticMeshes, runtimeAssets.Materials, runtimeAssets.MaterialSets);
        pipeline->AddMeshRenderFeature(graphics);
    }

#ifdef SENCHA_ENABLE_DEBUG_UI
    ConsoleService& console = engine.Console();
    auto& windows = engine.Platform().Windows;
    SdlWindow* window = windows.GetPrimaryWindow();
    auto& instance = graphics.Instance;
    auto& frames = graphics.Frames;

    auto debugOverlay =
        std::make_unique<ImGuiDebugOverlay>(debug, *window, instance, frames);
    debugOverlay->AddPanel<ConsolePanel>(debugLog, console);
    debugOverlay->AddPanel<TimingPanel>(engine.Timing());
    if (pipeline != nullptr)
    {
        debugOverlay->AddPanel<CubeDemoPanel>(
            pipeline->GetRenderQueue(), pipeline->GetCameraData(), FreeCam, DemoRegistry, Demo);
    }
    DebugOverlay = debugOverlay.get();
    auto& renderer = graphics.MainRenderer;
    renderer.AddFeature(std::move(debugOverlay));
#else
    (void)debugLog;
    debug.Open();
#endif

    std::printf("Sencha Cube Demo\n");
    std::printf("  Right mouse + move: look\n");
    std::printf("  WASD: move, Q/E: down/up, Shift: fast\n");
    std::printf("  F1: pause simulation (timescale 0)\n");
    std::printf("  `: debugger\n");
    std::printf("  Escape: quit\n");
}

void CubeDemoGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    CaptionRuntime* captions = &GetEngine().Captions();
    RegisterCubeDemoSystems(ctx.Schedule, DemoRegistry, FreeCam, Demo, captions);
#ifdef SENCHA_ENABLE_COOK
    ctx.Schedule.Register<HotReloadPollSystem>(Watcher, Reloader);
#endif
    if (const uint32_t autoExitFrames = ReadAutoExitFrameCount(); autoExitFrames > 0)
        ctx.Schedule.Register<AutoExitSystem>(GetEngine(), autoExitFrames);
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

void CubeDemoGame::OnShutdown(GameShutdownContext& ctx)
{
#ifdef SENCHA_ENABLE_DEBUG_UI
    DebugOverlay = nullptr;
#endif
    SetRelativeMouseMode(GetEngine(), false);

    // Best-effort cancel if the load is still in flight; if the build is
    // mid-run on the task thread, Engine::Shutdown drops the undrained
    // commit, so the zone never attaches either way.
    if (ZoneLoader && ZoneLoader->IsLoading(ZoneId{ 1 }))
        ZoneLoader->CancelLoad(ZoneId{ 1 });
    ZoneLoader.reset();

    GetEngine().Zones().DestroyZone(ZoneId{ 1 });
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
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;

    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) == enabled)
        return;

    SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);
}
