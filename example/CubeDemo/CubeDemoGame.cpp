#include "CubeDemoGame.h"

#include "CubeDemoSystems.h"

#include <app/Engine.h>
#include <world/serialization/SceneSerializer.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
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
                      Registry& registry,
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
            ImGui::Text("Right mouse: look");
            ImGui::Text("WASD: move, Q/E: down/up, Shift: fast");
            ImGui::Separator();
            ImGui::Text("Opaque queue: %zu", Queue.Opaque().size());
            ImGui::Text("Camera entity: %u", Camera.Entity.Index);
            ImGui::DragFloat("Move speed", &FreeCam.MoveSpeed, 0.1f, 0.1f, 50.0f);
            ImGui::DragFloat("Mouse sensitivity", &FreeCam.MouseSensitivity, 0.0001f, 0.0001f, 0.02f);

            if (Transform3f* cube = DemoTransforms(RegistryInstance).TryGetLocalMutable(Scene.CenterCube))
                ImGui::DragFloat3("Center cube position", &cube->Position.X, 0.05f);

            ImGui::End();
        }

    private:
        RenderQueue& Queue;
        CameraRenderData& Camera;
        FreeCamera& FreeCam;
        Registry& RegistryInstance;
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

    Assets.emplace(logging, buffers);
    RuntimeAssets& runtimeAssets = RuntimeAssetState();

    DemoRegistry = &CreateDefault3DZone(
        engine.Zones(), ZoneId{ 1 },
        ZoneParticipation{ .Visible = true, .Logic = true },
        &runtimeAssets.Meshes, &runtimeAssets.Materials);

    InitSceneSerializer();
    Demo = LoadDemoScene(*DemoRegistry, runtimeAssets.Assets, FreeCam, "cube_demo_scene.json");

    engine.AddDefaultMeshRenderFeature(runtimeAssets.Meshes, runtimeAssets.Materials);

#ifdef SENCHA_ENABLE_DEBUG_UI
    auto& windows = services.Get<SdlWindowService>();
    SdlWindow* window = windows.GetPrimaryWindow();
    auto& instance = services.Get<VulkanInstanceService>();
    auto& frames = services.Get<VulkanFrameService>();

    auto debugOverlay =
        std::make_unique<ImGuiDebugOverlay>(debug, *window, instance, frames);
    debugOverlay->AddPanel<ConsolePanel>(debugLog);
    debugOverlay->AddPanel<TimingPanel>(engine.Timing());
    debugOverlay->AddPanel<CubeDemoPanel>(
        engine.GetRenderQueue(), engine.GetCameraData(), FreeCam, *DemoRegistry, Demo);
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
