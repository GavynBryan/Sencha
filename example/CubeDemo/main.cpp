#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <entity/EntityRegistry.h>
#include <graphics/Renderer.h>
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
#include <math/Quat.h>
#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshRenderFeature.h>
#include <render/MeshService.h>
#include <render/RenderExtractionSystem.h>
#include <render/RenderQueue.h>
#include <registry/Registry.h>
#include <time/TimeService.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationSystem.h>
#include <transform/TransformStore.h>
#include <window/SdlVideoService.h>
#include <window/SdlWindow.h>
#include <window/SdlWindowService.h>
#include <window/WindowCreateInfo.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/IDebugPanel.h>
#include <debug/ImGuiDebugOverlay.h>
#include <imgui.h>
#endif

#include <SDL3/SDL.h>

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

    struct FreeCamera
    {
        EntityId Entity;
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        float MoveSpeed = 4.0f;
        float FastMultiplier = 4.0f;
        float MouseSensitivity = 0.0025f;
        bool LookHeld = false;

        void HandleEvent(const SDL_Event& event)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                && event.button.button == SDL_BUTTON_RIGHT)
            {
                LookHeld = true;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
                     && event.button.button == SDL_BUTTON_RIGHT)
            {
                LookHeld = false;
            }
            else if (LookHeld && event.type == SDL_EVENT_MOUSE_MOTION)
            {
                Yaw -= static_cast<float>(event.motion.xrel) * MouseSensitivity;
                Pitch -= static_cast<float>(event.motion.yrel) * MouseSensitivity;
                Pitch = SDL_clamp(Pitch, -1.5f, 1.5f);
            }
        }

        void Update(TransformStore<Transform3f>& transforms, float dt)
        {
            Transform3f* transform = transforms.TryGetLocalMutable(Entity);
            if (transform == nullptr) return;

            const bool* keys = SDL_GetKeyboardState(nullptr);
            Vec3d move = Vec3d::Zero();
            if (keys[SDL_SCANCODE_W]) move += Vec3d::Forward();
            if (keys[SDL_SCANCODE_S]) move += Vec3d::Backward();
            if (keys[SDL_SCANCODE_D]) move += Vec3d::Right();
            if (keys[SDL_SCANCODE_A]) move += Vec3d::Left();
            if (keys[SDL_SCANCODE_E]) move += Vec3d::Up();
            if (keys[SDL_SCANCODE_Q]) move += Vec3d::Down();

            transform->Rotation =
                Quatf::FromAxisAngle(Vec3d::Up(), Yaw)
                * Quatf::FromAxisAngle(Vec3d::Right(), Pitch);

            if (move.SqrMagnitude() > 0.0f)
            {
                move = move.Normalized();
                const float speed = MoveSpeed
                    * (keys[SDL_SCANCODE_LSHIFT] ? FastMultiplier : 1.0f);
                transform->Position += transform->Rotation.RotateVector(move) * (speed * dt);
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

    bool RecreateSwapchain(SdlWindowService& windows,
                           SdlWindowService::WindowId windowId,
                           VulkanSwapchainService& swapchain,
                           VulkanFrameService& frames,
                           Renderer& renderer)
    {
        WindowExtent extent = windows.GetExtent(windowId);
        if (extent.Width == 0 || extent.Height == 0)
        {
            return false;
        }

        if (!swapchain.Recreate(extent))
        {
            return false;
        }

        frames.ResetAfterSwapchainRecreate();
        renderer.NotifySwapchainRecreated();
        return true;
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
    DemoScene demoScene = CreateScene(scene, hierarchy, transforms, renderers, cameras,
                                      activeCamera, meshes, materials, freeCamera);

    TransformPropagationSystem<Transform3f> transformPropagation(
        transforms, hierarchy, propagationOrder);

    Renderer renderer(logging, device, physicalDevice, queues, swapchain, frames,
                      allocator, buffers, images, samplers, shaders, pipelines,
                      descriptors, scratch, upload);
    renderer.AddFeature(std::make_unique<MeshRenderFeature>(
        renderQueue, meshes, materials, cameraData));

#ifdef SENCHA_ENABLE_DEBUG_UI
    auto debugOverlay = std::make_unique<ImGuiDebugOverlay>(debug, *window, instance, frames);
    debugOverlay->AddPanel<ConsolePanel>(debugLog);
    debugOverlay->AddPanel<CubeDemoPanel>(
        renderQueue, cameraData, freeCamera, transforms, demoScene);
    ImGuiDebugOverlay* debugOverlayPtr = debugOverlay.get();
    renderer.AddFeature(std::move(debugOverlay));
#else
    debug.Open();
    ImGuiDebugOverlay* debugOverlayPtr = nullptr;
#endif

    const SdlWindowService::WindowId windowId = windows.GetPrimaryWindowId();
    TimeService time;
    bool running = true;

    std::printf("Sencha Cube Demo\n");
    std::printf("  Right mouse + move: look\n");
    std::printf("  WASD: move, Q/E: down/up, Shift: fast\n");
    std::printf("  `: debugger when built with SENCHA_ENABLE_DEBUG_UI=ON\n");
    std::printf("  Escape: quit\n");

    while (running)
    {
        const float dt = time.Advance().Dt;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);

#ifdef SENCHA_ENABLE_DEBUG_UI
            if (debugOverlayPtr != nullptr && debugOverlayPtr->ProcessSdlEvent(event))
            {
                continue;
            }
#endif

            freeCamera.HandleEvent(event);

            if (event.type == SDL_EVENT_QUIT
                || (event.type == SDL_EVENT_KEY_DOWN
                    && !event.key.repeat
                    && event.key.scancode == SDL_SCANCODE_ESCAPE))
            {
                running = false;
            }
        }

        if (windows.IsCloseRequested(windowId))
        {
            running = false;
        }

        WindowExtent resizedExtent;
        if (windows.ConsumeResize(windowId, &resizedExtent))
        {
            RecreateSwapchain(windows, windowId, swapchain, frames, renderer);
        }

        freeCamera.Update(transforms, dt);
        if (Transform3f* cube = transforms.TryGetLocalMutable(demoScene.CenterCube))
        {
            cube->Rotation *= Quatf::FromAxisAngle(Vec3d::Up(), dt);
        }

        transformPropagation.Propagate();
        if (!CameraRenderDataSystem::Build(
                activeCamera, cameras, transforms, swapchain.GetExtent(), cameraData))
        {
            continue;
        }

        renderQueue.Reset();
        RenderExtractionSystem::Extract(
            transforms, renderers, meshes, materials, cameraData, renderQueue);
        FrustumCullingSystem::Cull(cameraData, renderQueue);
        renderQueue.SortOpaque();

        const Renderer::DrawStatus status = renderer.DrawFrame();
        if (status == Renderer::DrawStatus::SwapchainOutOfDate)
        {
            RecreateSwapchain(windows, windowId, swapchain, frames, renderer);
        }
        else if (status == Renderer::DrawStatus::Error)
        {
            running = false;
        }
    }

    return 0;
}
