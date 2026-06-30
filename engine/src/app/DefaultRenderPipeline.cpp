#include <app/DefaultRenderPipeline.h>

#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <render/MeshRenderFeature.h>
#endif

#include <memory>
#include <unordered_set>

DefaultRenderPipeline::DefaultRenderPipeline(LoggingProvider* logging)
    : Log(logging ? &logging->GetLogger<DefaultRenderPipeline>() : nullptr)
{}

void DefaultRenderPipeline::SetAssetStores(StaticMeshCache& meshes,
                                           MaterialCache& materials,
                                           MaterialSetCache& materialSets)
{
    Meshes = &meshes;
    Materials = &materials;
    MaterialSets = &materialSets;
}

bool DefaultRenderPipeline::AddMeshRenderFeature(GraphicsServices& graphics)
{
#ifdef SENCHA_ENABLE_VULKAN
    if (Meshes == nullptr || Materials == nullptr)
        return false;

    Swapchain = &graphics.Swapchain;
    return graphics.MainRenderer.AddFeature(
        std::make_unique<MeshRenderFeature>(Queue, *Meshes, *Materials, Camera, Lights)) != nullptr;
#else
    (void)graphics;
    return false;
#endif
}

void DefaultRenderPipeline::ExtractRender(RenderExtractContext& ctx)
{
    if (Meshes == nullptr || Materials == nullptr || MaterialSets == nullptr)
        return;

#ifdef SENCHA_ENABLE_VULKAN
    if (Swapchain == nullptr)
    {
        ctx.PacketWrite.Renderable = false;
        return;
    }
    const VkExtent2D extent = Swapchain->GetExtent();
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

        auto* activeCamera = registry->Resources.TryGet<ActiveCameraService>();

        if (activeCamera == nullptr
            || !registry->Components.IsRegistered<CameraComponent>()
            || !registry->Components.IsRegistered<WorldTransform>())
            continue;

        hasCamera = CameraRenderDataSystem::Build(
            *activeCamera, registry->Components, extent, Camera);
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

        if (!registry->Components.IsRegistered<WorldTransform>()
            || !registry->Components.IsRegistered<StaticMeshComponent>())
            continue;

        RenderExtractor.Extract(registry->Components, *Meshes, *Materials, *MaterialSets, Camera, Queue);
    }

    Queue.SortOpaque();

    Lights.Reset();
    for (Registry* registry : ctx.ActiveRegistries)
    {
        if (registry == nullptr)
            continue;
        LightExtractor.Extract(registry->Components, Lights);
    }

    if (Log != nullptr)
    {
        if (Lights.Count == kMaxForwardLights && !LightCapWarned)
        {
            Log->Warn("Light cap ({}) reached; lights beyond cap dropped this frame", kMaxForwardLights);
            LightCapWarned = true;
        }
        else if (Lights.Count < kMaxForwardLights)
        {
            LightCapWarned = false;
        }
    }

    ctx.PacketWrite.Camera = Camera;
    ctx.PacketWrite.HasCamera = true;
    ctx.PacketWrite.Renderable = true;
}
