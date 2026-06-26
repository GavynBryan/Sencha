#include "SceneViewerGame.h"

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
#include <math/geometry/3d/Transform3d.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <render/Camera.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializer.h>
#include <zone/DefaultZoneBuilder.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <debug/ImGuiDebugOverlay.h>
#include <debug/TimingPanel.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#endif

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // Content layout: authored assets under "assets", cooked artifacts under
    // "assets/.cooked" (the cook's assetsRoot/.cooked). The authored scan skips
    // .cooked on its own; the cooked scan is a second root so each generated
    // mesh maps to the exact asset:// path the cook stamped.
    constexpr std::string_view kAuthoredRoot = "assets";
    constexpr std::string_view kCookedScanRoot = "assets/.cooked";
    constexpr ZoneId kPlayZone{ 1 };

    // The async load splits at the thread boundary (docs/ecs/parallelization.md):
    // file IO + JSON parse run on the task thread (this struct carries the
    // result), deserialization + camera wiring run on the main thread in the
    // attach commit.
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
} // namespace

void SceneViewerGame::OnRegisterComponents(ComponentSerializerRegistry&)
{
    // Built-in scene components only (Transform, StaticMesh, Camera, ...); the
    // viewer defines no game components. Registers into the engine's single
    // (shared-library) registry, which the param also refers to.
    InitSceneSerializer();
}

void SceneViewerGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(logging, graphics.Buffers, graphics.Images, graphics.Descriptors, graphics.Samplers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    // Mount: authored assets, then the cooked overlay (cooked wins). Same
    // resolution path proven headless (the cook's CookedSceneFullyResolves test).
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
            logging.GetLogger<SceneViewerGame>().Warn(
                "SceneViewer: no asset id map ({}); refs resolve by path only", idMapError);
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

#ifdef SENCHA_ENABLE_DEBUG_UI
    DebugService& debug = engine.Debug();
    SdlWindow* window = engine.Platform().Windows.GetPrimaryWindow();
    auto overlay = std::make_unique<ImGuiDebugOverlay>(
        debug, *window, graphics.Instance, graphics.Frames);
    overlay->AddPanel<ConsolePanel>(debug.GetLogSink(), engine.Console());
    overlay->AddPanel<TimingPanel>(engine.Timing());
    DebugOverlay = overlay.get();
    graphics.MainRenderer.AddFeature(std::move(overlay));
#endif

    std::printf("Sencha scene viewer\n");
    std::printf("  Load a map: +map levels/<name> (cooked under assets/.cooked/)\n");
    std::printf("  Right mouse: look | WASD: move | Q/E: down/up | Shift: fast | Escape: quit\n");
}

ConsoleResult SceneViewerGame::LoadMap(std::string_view mapName)
{
    Engine& engine = GetEngine();
    LoggingProvider& logging = engine.Logging();
    RuntimeAssets& runtimeAssets = RuntimeAssetState();
    Logger& log = logging.GetLogger<SceneViewerGame>();

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
        log.Warn("SceneViewer: no manifest for '{}' ({}); resolve-on-attach",
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
            Logger& finalizeLog = logging.GetLogger<SceneViewerGame>();
            if (!parsed->Json)
            {
                finalizeLog.Error("SceneViewer: {}", parsed->Error);
                return;
            }

            SceneLoadError loadError;
            SceneSerializationContext sceneContext(logging, &RuntimeAssetState().Assets);
            if (!LoadSceneJson(*parsed->Json, registry, sceneContext, &loadError))
            {
                finalizeLog.Error("SceneViewer: scene load error: {}", loadError.Message);
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

void SceneViewerGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    ctx.Schedule.Register<FreeCameraLookSystem>(ActiveZoneRegistry, FreeCam);
    ctx.Schedule.Register<FreeCameraMovementSystem>(ActiveZoneRegistry, FreeCam);
}

void SceneViewerGame::OnPlatformEvent(PlatformEventContext& ctx)
{
#ifdef SENCHA_ENABLE_DEBUG_UI
    if (DebugOverlay != nullptr && DebugOverlay->ProcessSdlEvent(ctx.Event))
        ctx.Handled = true;
#endif
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

void SceneViewerGame::OnShutdown(GameShutdownContext&)
{
#ifdef SENCHA_ENABLE_DEBUG_UI
    DebugOverlay = nullptr;
#endif
    SetRelativeMouseMode(false);

    if (ZoneLoader && ZoneLoader->IsLoading(kPlayZone))
        ZoneLoader->CancelLoad(kPlayZone);
    ZoneLoader.reset();

    GetEngine().Zones().DestroyZone(kPlayZone);
    ActiveZoneRegistry = nullptr;
}

RuntimeAssets& SceneViewerGame::RuntimeAssetState()
{
    assert(Assets.has_value() && "RuntimeAssets must be constructed before use");
    return *Assets;
}

void SceneViewerGame::SetRelativeMouseMode(bool enabled)
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
    static SceneViewerGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
