#include <app/DefaultSceneBinding.h>

#include <core/service/ServiceHost.h>
#include <render/RenderExtractionSystem.h>

#include <memory>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <render/MeshRenderFeature.h>
#endif

void DefaultSceneBinding::Reset()
{
    Scene = {};
    Queue.Reset();
    Camera = {};
}

void DefaultSceneBinding::RegisterScene(DefaultRenderScene scene)
{
    Scene = scene;
}

bool DefaultSceneBinding::AddMeshRenderFeature(ServiceHost& services,
                                               MeshService& meshes,
                                               MaterialStore& materials)
{
#ifdef SENCHA_ENABLE_VULKAN
    Renderer* renderer = services.TryGet<Renderer>();
    if (renderer == nullptr)
        return false;

    return renderer->AddFeature(
        std::make_unique<MeshRenderFeature>(Queue, meshes, materials, Camera)).IsValid();
#else
    (void)services;
    (void)meshes;
    (void)materials;
    return false;
#endif
}

bool DefaultSceneBinding::Extract(ServiceHost& services, RenderExtractContext& ctx)
{
    if (!Scene.IsValid())
        return false;

#ifdef SENCHA_ENABLE_VULKAN
    auto& swapchain = services.Get<VulkanSwapchainService>();

    if (!CameraRenderDataSystem::Build(
            *Scene.ActiveCamera,
            *Scene.Cameras,
            *Scene.Transforms,
            swapchain.GetExtent(),
            Camera))
    {
        ctx.PacketWrite.Renderable = false;
        return true;
    }

    ctx.PacketWrite.Camera = Camera;
    ctx.PacketWrite.HasCamera = true;

    Queue.Reset();
    RenderExtractionSystem::Extract(
        *Scene.Transforms,
        *Scene.Renderers,
        *Scene.Meshes,
        *Scene.Materials,
        Camera,
        Queue);
    FrustumCullingSystem::Cull(Camera, Queue);
    Queue.SortOpaque();
    ctx.PacketWrite.Renderable = true;
    return true;
#else
    (void)services;
    (void)ctx;
    return false;
#endif
}
