#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <world/entity/EntityRegistry.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeletionQueueService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <graphics/vulkan/VulkanSurfaceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>
#include <input/InputFrame.h>
#include <input/SdlInputCapture.h>
#include <math/Quat.h>
#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshRenderFeature.h>
#include <render/MeshService.h>
#include <render/RenderExtractionSystem.h>
#include <render/RenderQueue.h>
#include <runtime/FrameDriver.h>
#include <runtime/FrameTrace.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <time/TimingHistory.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationSystem.h>
#include <world/transform/TransformPresentationStore.h>
#include <world/transform/TransformStore.h>
#include <platform/SdlVideoService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/IDebugPanel.h>
#include <debug/ImGuiDebugOverlay.h>
#include <debug/TimingPanel.h>
#include <imgui.h>
#endif

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <memory>

namespace
{
    struct DemoScene
    {
        EntityId Camera;
        EntityId CenterCube;
        MeshHandle CubeMesh;
        MaterialHandle Red;
        MaterialHandle Green;
        MaterialHandle Blue;
    };

    // Free-look camera. Input is delivered via the InputFrame snapshot — SDL
    // state queries never reach this struct. Mouse look consumes deltas on
    // every presentation frame (visual feel); movement integrates on fixed
    // ticks (deterministic).
    struct FreeCamera
    {
        EntityId Entity;
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        float MoveSpeed = 4.0f;
        float FastMultiplier = 4.0f;
        float MouseSensitivity = 0.0025f;
        bool LookHeld = false;

        void UpdateLook(const InputFrame& input)
        {
            LookHeld = input.IsMouseButtonDown(SDL_BUTTON_RIGHT);
            if (!LookHeld) return;
            Yaw -= input.MouseDeltaX * MouseSensitivity;
            Pitch -= input.MouseDeltaY * MouseSensitivity;
            Pitch = SDL_clamp(Pitch, -1.5f, 1.5f);
        }

        void TickFixed(const InputFrame& input,
                       TransformStore<Transform3f>& transforms,
                       float fixedDt)
        {
            Transform3f* transform = transforms.TryGetLocalMutable(Entity);
            if (transform == nullptr) return;

            Vec3d move = Vec3d::Zero();
            if (input.IsKeyDown(SDL_SCANCODE_W)) move += Vec3d::Forward();
            if (input.IsKeyDown(SDL_SCANCODE_S)) move += Vec3d::Backward();
            if (input.IsKeyDown(SDL_SCANCODE_D)) move += Vec3d::Right();
            if (input.IsKeyDown(SDL_SCANCODE_A)) move += Vec3d::Left();
            if (input.IsKeyDown(SDL_SCANCODE_E)) move += Vec3d::Up();
            if (input.IsKeyDown(SDL_SCANCODE_Q)) move += Vec3d::Down();

            transform->Rotation =
                Quatf::FromAxisAngle(Vec3d::Up(), Yaw)
                * Quatf::FromAxisAngle(Vec3d::Right(), Pitch);

            if (move.SqrMagnitude() > 0.0f)
            {
                move = move.Normalized();
                const float speed = MoveSpeed
                    * (input.IsKeyDown(SDL_SCANCODE_LSHIFT) ? FastMultiplier : 1.0f);
                transform->Position += transform->Rotation.RotateVector(move) * (speed * fixedDt);
            }
        }
    };

    void AddCube(EntityRegistry& entities,
                 TransformHierarchyService& hierarchy,
                 TransformStore<Transform3f>& transforms,
                 MeshRendererStore& renderers,
                 MeshHandle mesh,
                 MaterialHandle material,
                 const Vec3d& position,
                 const Vec3d& scale)
    {
        EntityId entity = entities.Create();
        hierarchy.Register(entity);
        transforms.Add(entity, Transform3f(position, Quatf::Identity(), scale));
        renderers.Add(entity, MeshRendererComponent{
            .Mesh = mesh,
            .Material = material,
        });
    }

    DemoScene CreateScene(Registry& registry,
                          TransformHierarchyService& hierarchy,
                          TransformStore<Transform3f>& transforms,
                          MeshRendererStore& renderers,
                          CameraStore& cameras,
                          ActiveCameraService& activeCamera,
                          MeshService& meshes,
                          MaterialStore& materials,
                          FreeCamera& freeCamera)
    {
        DemoScene scene;
        scene.CubeMesh = meshes.Create(MeshPrimitives::BuildCube(1.0f));
        scene.Red = materials.Create(Material{
            .Pass = ShaderPassId::ForwardOpaque,
            .BaseColor = Vec4(1.0f, 0.15f, 0.1f, 1.0f),
        });
        scene.Green = materials.Create(Material{
            .Pass = ShaderPassId::ForwardOpaque,
            .BaseColor = Vec4(0.1f, 0.85f, 0.45f, 1.0f),
        });
        scene.Blue = materials.Create(Material{
            .Pass = ShaderPassId::ForwardOpaque,
            .BaseColor = Vec4(0.2f, 0.45f, 1.0f, 1.0f),
        });

        scene.CenterCube = registry.Entities.Create();
        hierarchy.Register(scene.CenterCube);
        transforms.Add(scene.CenterCube, Transform3f(
            Vec3d(0.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
        renderers.Add(scene.CenterCube, MeshRendererComponent{
            .Mesh = scene.CubeMesh,
            .Material = scene.Red,
        });

        AddCube(registry.Entities, hierarchy, transforms, renderers,
                scene.CubeMesh, scene.Green, Vec3d(-2.2f, 0.0f, -1.0f), Vec3d::One());
        AddCube(registry.Entities, hierarchy, transforms, renderers,
                scene.CubeMesh, scene.Blue, Vec3d(2.2f, 0.0f, -1.0f), Vec3d(0.75f, 1.5f, 0.75f));
        AddCube(registry.Entities, hierarchy, transforms, renderers,
                scene.CubeMesh, scene.Green, Vec3d(0.0f, -1.4f, -3.0f), Vec3d(8.0f, 0.15f, 8.0f));

        scene.Camera = registry.Entities.Create();
        hierarchy.Register(scene.Camera);
        transforms.Add(scene.Camera, Transform3f(
            Vec3d(0.0f, 1.0f, 5.0f), Quatf::Identity(), Vec3d::One()));
        cameras.Add(scene.Camera, CameraComponent{
            .Projection = ProjectionKind::Perspective,
            .FovYRadians = 1.22173048f,
            .NearPlane = 0.1f,
            .FarPlane = 1000.0f,
        });
        activeCamera.SetActive(scene.Camera);

        freeCamera.Entity = scene.Camera;
        return scene;
    }

#ifdef SENCHA_ENABLE_DEBUG_UI
    class CubeDemoPanel : public IDebugPanel
    {
    public:
        CubeDemoPanel(RenderQueue& queue,
                      CameraRenderData& camera,
                      FreeCamera& freeCamera,
                      TransformStore<Transform3f>& transforms,
                      DemoScene& scene)
            : Queue(queue)
            , Camera(camera)
            , FreeCam(freeCamera)
            , Transforms(transforms)
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

            if (Transform3f* cube = Transforms.TryGetLocalMutable(Scene.CenterCube))
            {
                ImGui::DragFloat3("Center cube position", &cube->Position.X, 0.05f);
            }
            ImGui::End();
        }

    private:
        RenderQueue& Queue;
        CameraRenderData& Camera;
        FreeCamera& FreeCam;
        TransformStore<Transform3f>& Transforms;
        DemoScene& Scene;
    };
#endif

    using DemoClock = std::chrono::steady_clock;

    double SecondsSince(DemoClock::time_point start)
    {
        return std::chrono::duration<double>(DemoClock::now() - start).count();
    }
}

int main()
{
    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();
    DebugLogSink& debugLog = logging.AddSink<DebugLogSink>();
    DebugService debug(logging, debugLog);

    SdlVideoService video(logging);
    SdlWindowService windows(logging, video);

    WindowCreateInfo windowInfo;
    windowInfo.Title = "Sencha Cube Demo";
    windowInfo.Width = 1280;
    windowInfo.Height = 720;
    windowInfo.GraphicsApi = WindowGraphicsApi::Vulkan;
    SdlWindow* window = windows.CreateWindow(windowInfo);
    if (window == nullptr || !window->IsValid())
    {
        std::fprintf(stderr, "Failed to create Vulkan window.\n");
        return 1;
    }

    VulkanBootstrapPolicy policy;
    policy.AppName = "Sencha Cube Demo";
    policy.RequiredQueues.Present = true;
    policy.RequiredInstanceExtensions = windows.GetRequiredVulkanInstanceExtensions();
    policy.RequiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VulkanInstanceService instance(logging, policy);
    VulkanSurfaceService surface(logging, instance, *window);
    VulkanPhysicalDeviceService physicalDevice(logging, instance, policy, &surface);
    VulkanDeviceService device(logging, physicalDevice, policy);
    VulkanQueueService queues(logging, device, physicalDevice, policy);
    VulkanAllocatorService allocator(logging, instance, physicalDevice, device);
    VulkanUploadContextService upload(logging, device, queues);
    VulkanDeletionQueueService deletionQueue(logging, 2);
    VulkanBufferService buffers(logging, device, allocator, upload);
    VulkanImageService images(logging, device, allocator, upload, deletionQueue);
    VulkanSamplerCache samplers(logging, device);
    VulkanShaderCache shaders(logging, device);
    VulkanPipelineCache pipelines(logging, device, shaders);
    VulkanDescriptorCache descriptors(logging, device, buffers, images);
    VulkanFrameScratch scratch(logging, device, physicalDevice, buffers, {});
    VulkanSwapchainService swapchain(logging, device, physicalDevice, surface, queues, window->GetExtent());
    VulkanFrameService frames(logging, device, queues, swapchain, deletionQueue, 2);

    if (!instance.IsValid() || !surface.IsValid() || !physicalDevice.IsValid()
        || !device.IsValid() || !queues.IsValid() || !allocator.IsValid()
        || !upload.IsValid() || !buffers.IsValid() || !images.IsValid()
        || !samplers.IsValid() || !shaders.IsValid() || !pipelines.IsValid()
        || !descriptors.IsValid() || !scratch.IsValid() || !swapchain.IsValid()
        || !frames.IsValid())
    {
        std::fprintf(stderr, "Failed to initialize Vulkan demo services.\n");
        return 1;
    }

    TransformHierarchyService hierarchy;
    TransformPropagationOrderService propagationOrder;
    Registry scene = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
    auto& transforms = scene.Components.Register<TransformStore<Transform3f>>(propagationOrder);
    auto& renderers = scene.Components.Register<MeshRendererStore>();
    auto& cameras = scene.Components.Register<CameraStore>();
    ActiveCameraService activeCamera;
    MaterialStore materials;
    MeshService meshes(logging, buffers);
    RenderQueue renderQueue;
    CameraRenderData cameraData;
    FreeCamera freeCamera;
    TransformPresentationStore<Transform3f> presentationTransforms;
    DemoScene demoScene = CreateScene(scene, hierarchy, transforms, renderers, cameras,
                                      activeCamera, meshes, materials, freeCamera);

    TransformPropagationSystem<Transform3f> transformPropagation(
        transforms, hierarchy, propagationOrder);

    Renderer renderer(logging, device, physicalDevice, queues, swapchain, frames,
                      allocator, buffers, images, samplers, shaders, pipelines,
                      descriptors, scratch, upload);
    renderer.AddFeature(std::make_unique<MeshRenderFeature>(
        renderQueue, meshes, materials, cameraData));

    TimingHistory timingHistory(512);
#ifdef SENCHA_ENABLE_DEBUG_UI
    auto debugOverlay = std::make_unique<ImGuiDebugOverlay>(debug, *window, instance, frames);
    debugOverlay->AddPanel<ConsolePanel>(debugLog);
    debugOverlay->AddPanel<TimingPanel>(timingHistory);
    debugOverlay->AddPanel<CubeDemoPanel>(
        renderQueue, cameraData, freeCamera, transforms, demoScene);
    ImGuiDebugOverlay* debugOverlayPtr = debugOverlay.get();
    renderer.AddFeature(std::move(debugOverlay));
#else
    debug.Open();
    ImGuiDebugOverlay* debugOverlayPtr = nullptr;
#endif

    const SdlWindowService::WindowId windowId = windows.GetPrimaryWindowId();
    RuntimeFrameLoop runtime;
    runtime.SetSurfaceExtent(window->GetExtent());
    presentationTransforms.Reset(transforms, hierarchy, propagationOrder);

    // Subscribe presentation transforms to the discontinuity bus. Every
    // swapchain recreate, minimize/restore, or teleport will snap the
    // interpolation history so the next render frame does not smear.
    runtime.GetDiscontinuityBus().Subscribe(
        [&](const FrameDiscontinuityEvent&) {
            presentationTransforms.Reset(transforms, hierarchy, propagationOrder);
        });

    FrameDriver driver(runtime);
    driver.SetTimingHistory(&timingHistory);
    // Target 0.0 = uncapped when in FIFO (VSync caps us). Bump to cap IMMEDIATE.
    driver.SetTargetFps(0.0);

    bool running = true;

    // -- Phase registrations ----------------------------------------------
    // PumpPlatform: drain SDL events, build InputFrame, route lifecycle.
    driver.Register(FramePhase::PumpPlatform, [&](PhaseContext& ctx) {
        SdlInputCapture::BeginFrame(*ctx.Input);
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);
#ifdef SENCHA_ENABLE_DEBUG_UI
            if (debugOverlayPtr != nullptr && debugOverlayPtr->ProcessSdlEvent(event))
                continue;
#endif
            SdlInputCapture::Accept(*ctx.Input, event);

            if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                ctx.Runtime->NotifyMinimized();
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
                ctx.Runtime->NotifyRestored(windows.GetExtent(windowId));
        }

        if (windows.IsCloseRequested(windowId))
            ctx.Input->QuitRequested = true;
        if (ctx.Input->IsKeyDown(SDL_SCANCODE_ESCAPE))
            ctx.Input->QuitRequested = true;

        // Debug pause: F1 toggles timescale 0 vs 1.
        static bool paused = false;
        for (uint32_t sc : ctx.Input->KeysPressed)
        {
            if (sc == SDL_SCANCODE_F1)
            {
                paused = !paused;
                ctx.Runtime->SetSimulationTimescale(paused ? 0.0f : 1.0f);
            }
        }
    });

    // ResolveLifecycle: window resize → swapchain dirty flag.
    driver.Register(FramePhase::ResolveLifecycle, [&](PhaseContext& ctx) {
        WindowExtent resizedExtent;
        if (windows.ConsumeResize(windowId, &resizedExtent))
            ctx.Runtime->NotifyResize(resizedExtent);

        const SdlWindowService::WindowState* windowState = windows.GetState(windowId);
        if (windowState != nullptr && windowState->Minimized)
            ctx.Runtime->NotifyMinimized();

        ctx.Runtime->ResolveLifecycleTransitions();
    });

    // RebuildGraphics: recreate swapchain on the graphics owner thread.
    driver.Register(FramePhase::RebuildGraphics, [&](PhaseContext& ctx) {
        if (ctx.Runtime->ShouldRebuildSwapchain())
        {
            const WindowExtent rebuildExtent = ctx.Runtime->GetDesiredSwapchainExtent();
            ctx.Runtime->BeginSwapchainRebuild();
            if (swapchain.Recreate(rebuildExtent))
            {
                frames.ResetAfterSwapchainRecreate();
                renderer.NotifySwapchainRecreated();
                ctx.Runtime->CompleteSwapchainRebuild(rebuildExtent);
            }
            else
            {
                ctx.Runtime->FailSwapchainRebuild();
            }
        }
    });

    // AdvanceEngineTime: sanitize dt + publish discontinuity events.
    driver.Register(FramePhase::AdvanceEngineTime, [&](PhaseContext& ctx) {
        ctx.Runtime->AdvanceEngineTime();
        ctx.Runtime->AccumulateSimulationTime();
    });

    // Simulate: called 0..N times per frame, inside the fixed loop.
    driver.Register(FramePhase::Simulate, [&](PhaseContext& ctx) {
        presentationTransforms.BeginSimulationTick(transforms);

        const float fixedDt = static_cast<float>(ctx.CurrentTick.DeltaSeconds);
        freeCamera.TickFixed(*ctx.Input, transforms, fixedDt);

        if (Transform3f* cube = transforms.TryGetLocalMutable(demoScene.CenterCube))
        {
            cube->Rotation *= Quatf::FromAxisAngle(Vec3d::Up(), fixedDt);
        }

        transformPropagation.Propagate();
        presentationTransforms.EndSimulationTick(transforms);
    });

    // ExtractRenderPacket: interpolate transforms + build draw list.
    driver.Register(FramePhase::ExtractRenderPacket, [&](PhaseContext& ctx) {
        freeCamera.UpdateLook(*ctx.Input);

        const float alpha = static_cast<float>(ctx.PacketWrite->Presentation.Alpha);
        presentationTransforms.BuildRenderSnapshot(
            transforms, hierarchy, propagationOrder, alpha);

        if (!CameraRenderDataSystem::Build(
                activeCamera, cameras, presentationTransforms,
                swapchain.GetExtent(), cameraData))
        {
            ctx.PacketWrite->Renderable = false;
            return;
        }
        ctx.PacketWrite->Camera = cameraData;
        ctx.PacketWrite->HasCamera = true;

        renderQueue.Reset();
        RenderExtractionSystem::Extract(
            presentationTransforms, renderers, meshes, materials, cameraData, renderQueue);
        FrustumCullingSystem::Cull(cameraData, renderQueue);
        renderQueue.SortOpaque();
        ctx.PacketWrite->Renderable = true;
    });

    // Render: submit + present.
    driver.Register(FramePhase::Render, [&](PhaseContext& ctx) {
        if (!ctx.PacketWrite->Renderable)
            return;

        const RenderFrameResult renderResult = renderer.DrawFrameScheduled();
        if (renderResult == RenderFrameResult::SwapchainOutOfDate
            || renderResult == RenderFrameResult::SurfaceSuboptimal)
        {
            ctx.Runtime->SetSurfaceExtent(windows.GetExtent(windowId));
            ctx.Runtime->NotifySwapchainInvalidated();
        }
        else if (renderResult == RenderFrameResult::Failed)
        {
            ctx.Input->QuitRequested = true;
        }

        // Stamp telemetry for the HUD.
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        const VulkanFrameTiming& vk = frames.GetLastTiming();
        const RendererFrameTiming& rt = renderer.GetLastTiming();
        const SwapchainState swap = swapchain.GetState();
        timingHistory.Push(TimingFrameSample{
            .RawDtSeconds = rf.PlatformTime.RawDeltaSeconds,
            .EngineDtSeconds = rf.EngineTime.SanitizedDeltaSeconds,
            .PresentationDtSeconds = rf.Presentation.DeltaSeconds,
            .FixedAccumulatorBeforeSeconds = rf.AccumulatorBeforeTicks,
            .FixedAccumulatorSeconds = rf.AccumulatorAfterTicks,
            .InterpolationAlpha = rf.Presentation.Alpha,
            .RenderRecordSeconds = rt.RecordSeconds,
            .AcquireSeconds = vk.AcquireSeconds,
            .SubmitSeconds = vk.SubmitSeconds,
            .PresentSeconds = vk.PresentSeconds,
            .TotalFrameSeconds = rt.TotalSeconds,
            .FixedTicks = rf.FixedTicks,
            .LifecycleState = static_cast<int>(rf.State),
            .TemporalDiscontinuityReason = static_cast<int>(rf.DiscontinuityReason),
            .RuntimeEvents = static_cast<uint32_t>(rf.Events),
            .RenderResult = static_cast<int>(renderResult),
            .Presented = renderResult == RenderFrameResult::Presented,
            .LifecycleOnly = rf.LifecycleOnly,
            .SwapchainGeneration = swap.Generation,
            .SwapchainRecreateCount = swapchain.GetRecreateCount(),
            .SwapchainImageIndex = vk.ImageIndex,
            .SwapchainImageCount = swap.ImageCount,
            .PresentMode = static_cast<int>(swap.PresentMode),
            .SwapchainRecreated = HasRuntimeFrameEvent(
                rf.Events, RuntimeFrameEventFlags::SwapchainRecreated),
            .PresentationDtSuppressed =
                rf.DiscontinuityReason != TemporalDiscontinuityReason::None,
        });
    });

    // EndFrame: for lifecycle-only frames, stamp a minimal telemetry row.
    driver.Register(FramePhase::EndFrame, [&](PhaseContext& ctx) {
        const RuntimeFrameSnapshot& rf = ctx.Runtime->GetCurrentFrame();
        if (!rf.LifecycleOnly) return;
        const SwapchainState swap = swapchain.GetState();
        timingHistory.Push(TimingFrameSample{
            .RawDtSeconds = rf.PlatformTime.RawDeltaSeconds,
            .EngineDtSeconds = rf.EngineTime.SanitizedDeltaSeconds,
            .PresentationDtSeconds = rf.Presentation.DeltaSeconds,
            .FixedAccumulatorBeforeSeconds = rf.AccumulatorBeforeTicks,
            .FixedAccumulatorSeconds = rf.AccumulatorAfterTicks,
            .InterpolationAlpha = rf.Presentation.Alpha,
            .FixedTicks = rf.FixedTicks,
            .LifecycleState = static_cast<int>(rf.State),
            .TemporalDiscontinuityReason = static_cast<int>(rf.DiscontinuityReason),
            .RuntimeEvents = static_cast<uint32_t>(rf.Events),
            .RenderResult = static_cast<int>(RenderFrameResult::SkippedMinimized),
            .Presented = false,
            .LifecycleOnly = true,
            .SwapchainGeneration = swap.Generation,
            .SwapchainRecreateCount = swapchain.GetRecreateCount(),
            .SwapchainImageCount = swap.ImageCount,
            .PresentMode = static_cast<int>(swap.PresentMode),
            .SwapchainRecreated = HasRuntimeFrameEvent(
                rf.Events, RuntimeFrameEventFlags::SwapchainRecreated),
            .PresentationDtSuppressed =
                rf.DiscontinuityReason != TemporalDiscontinuityReason::None,
        });
    });

    driver.SetShouldExit([&] { return !running; });

    std::printf("Sencha Cube Demo\n");
    std::printf("  Right mouse + move: look\n");
    std::printf("  WASD: move, Q/E: down/up, Shift: fast\n");
    std::printf("  F1: pause simulation (timescale 0)\n");
    std::printf("  `: debugger when built with SENCHA_ENABLE_DEBUG_UI=ON\n");
    std::printf("  Escape: quit\n");

    driver.Run();

    return 0;
}
