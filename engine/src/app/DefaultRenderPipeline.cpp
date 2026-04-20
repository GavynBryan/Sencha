#include <app/DefaultRenderPipeline.h>

#include <app/Engine.h>
#include <core/service/ServiceHost.h>
#include <render/RenderExtractionSystem.h>
#include <world/registry/Registry.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <render/MeshRenderFeature.h>
#endif

#include <memory>
#include <unordered_set>

void DefaultRenderPipeline::SetAssetStores(MeshCache& meshes, MaterialCache& materials)
{
    Meshes = &meshes;
    Materials = &materials;
}

bool DefaultRenderPipeline::AddMeshRenderFeature(ServiceHost& services)
{
#ifdef SENCHA_ENABLE_VULKAN
    Renderer* renderer = services.TryGet<Renderer>();
    if (renderer == nullptr || Meshes == nullptr || Materials == nullptr)
        return false;

    return renderer->AddFeature(
        std::make_unique<MeshRenderFeature>(Queue, *Meshes, *Materials, Camera)).IsValid();
#else
    (void)services;
    return false;
#endif
}

void DefaultRenderPipeline::ExtractRender(RenderExtractContext& ctx)
{
    if (Meshes == nullptr || Materials == nullptr)
        return;

#ifdef SENCHA_ENABLE_VULKAN
    auto& swapchain = ctx.EngineInstance.Services().Get<VulkanSwapchainService>();
    const VkExtent2D extent = swapchain.GetExtent();
#else
    (void)ctx;
    return;
#endif

    Queue.Reset();
    bool hasCamera = false;

    for (Registry* registry : ctx.ActiveRegistries)
    {
        if (registry == nullptr)
            continue;

        auto* transforms = registry->Components.TryGet<TransformStore<Transform3f>>();
        auto* cameras = registry->Components.TryGet<CameraStore>();
        auto* activeCamera = registry->Resources.TryGet<ActiveCameraService>();

        if (transforms == nullptr || cameras == nullptr || activeCamera == nullptr)
            continue;

        hasCamera = CameraRenderDataSystem::Build(
            *activeCamera, *cameras, *transforms, extent, Camera);
        if (hasCamera)
            break;
    }

    if (!hasCamera)
    {
        ctx.PacketWrite.Renderable = false;
        return;
    }

    for (Registry* registry : ctx.ActiveRegistries)
    {
        if (registry == nullptr)
            continue;

        auto* transforms = registry->Components.TryGet<TransformStore<Transform3f>>();
        auto* renderers = registry->Components.TryGet<MeshRendererStore>();

        if (transforms == nullptr || renderers == nullptr)
            continue;

        RenderExtractionSystem::Extract(*transforms, *renderers, *Meshes, *Materials, Camera, Queue);
    }

    FrustumCullingSystem::Cull(Camera, Queue);
    Queue.SortOpaque();

    ctx.PacketWrite.Camera = Camera;
    ctx.PacketWrite.HasCamera = true;
    ctx.PacketWrite.Renderable = true;
}
