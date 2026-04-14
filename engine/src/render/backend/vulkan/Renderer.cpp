#include <render/Renderer.h>

#include <render/backend/vulkan/VulkanAllocatorService.h>
#include <render/backend/vulkan/VulkanBarriers.h>
#include <render/backend/vulkan/VulkanBufferService.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanFrameScratch.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanQueueService.h>
#include <render/backend/vulkan/VulkanSamplerCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>
#include <render/backend/vulkan/VulkanSwapchainService.h>
#include <render/backend/vulkan/VulkanUploadContextService.h>

Renderer::Renderer(LoggingProvider& logging,
                   VulkanDeviceService& device,
                   VulkanPhysicalDeviceService& physicalDevice,
                   VulkanQueueService& queues,
                   VulkanSwapchainService& swapchain,
                   VulkanFrameService& frames,
                   VulkanAllocatorService& allocator,
                   VulkanBufferService& buffers,
                   VulkanImageService& images,
                   VulkanSamplerCache& samplers,
                   VulkanShaderCache& shaders,
                   VulkanPipelineCache& pipelines,
                   VulkanDescriptorCache& descriptors,
                   VulkanFrameScratch& scratch,
                   VulkanUploadContextService& upload)
    : Log(logging.GetLogger<Renderer>())
    , Swapchain(swapchain)
    , Frames(frames)
{
    if (!device.IsValid() || !physicalDevice.IsValid() || !queues.IsValid()
        || !swapchain.IsValid() || !frames.IsValid() || !allocator.IsValid()
        || !buffers.IsValid() || !images.IsValid() || !samplers.IsValid()
        || !shaders.IsValid() || !pipelines.IsValid() || !descriptors.IsValid()
        || !scratch.IsValid() || !upload.IsValid())
    {
        Log.Error("Cannot create Renderer: upstream services not valid");
        return;
    }

    Services.Logging = &logging;
    Services.Device = &device;
    Services.PhysicalDevice = &physicalDevice;
    Services.Queues = &queues;
    Services.Allocator = &allocator;
    Services.Buffers = &buffers;
    Services.Images = &images;
    Services.Samplers = &samplers;
    Services.Shaders = &shaders;
    Services.Pipelines = &pipelines;
    Services.Descriptors = &descriptors;
    Services.Scratch = &scratch;
    Services.Upload = &upload;

    ImageLayouts.assign(swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
    Valid = true;
}

Renderer::~Renderer()
{
    // Tear features down before any Vulkan service in our dependency list
    // starts unwinding. Order matters: features hold handles into the caches,
    // the caches own the VkDevice objects, and the device service outlives us.
    for (auto& feature : OwnedFeatures)
    {
        if (feature) feature->Teardown();
    }
    OwnedFeatures.clear();
    for (auto& bucket : PhaseBuckets)
    {
        bucket.clear();
    }
}

void Renderer::AddFeatureImpl(std::unique_ptr<IRenderFeature> feature)
{
    if (!Valid || feature == nullptr) return;

    const RenderPhase phase = feature->GetPhase();
    const size_t idx = static_cast<size_t>(phase);
    if (idx >= static_cast<size_t>(RenderPhase::Count))
    {
        Log.Error("Renderer::AddFeature: feature reports invalid phase ({})",
                  static_cast<int>(phase));
        return;
    }

    feature->Setup(Services);
    PhaseBuckets[idx].push_back(feature.get());
    OwnedFeatures.push_back(std::move(feature));
}

Renderer::DrawStatus Renderer::DrawFrame()
{
    if (!Valid) return DrawStatus::Error;

    VulkanFrame frame;
    const VulkanFrameStatus begin = Frames.BeginFrame(frame);
    if (begin == VulkanFrameStatus::SwapchainOutOfDate
        || begin == VulkanFrameStatus::SurfaceUnavailable)
    {
        return DrawStatus::SwapchainOutOfDate;
    }
    if (begin != VulkanFrameStatus::Ready)
    {
        return DrawStatus::Error;
    }

    // Rotate the per-frame scratch allocator into this frame's slice before
    // any feature draws -- feature code allocates transient UBOs from it.
    Services.Scratch->BeginFrame();

    RecordMainColorPhase(frame);

    const VulkanFrameStatus end = Frames.EndFrame(frame);
    if (end == VulkanFrameStatus::SwapchainOutOfDate
        || end == VulkanFrameStatus::SurfaceUnavailable)
    {
        return DrawStatus::SwapchainOutOfDate;
    }
    if (end != VulkanFrameStatus::Ready)
    {
        return DrawStatus::Error;
    }
    return DrawStatus::Ok;
}

void Renderer::NotifySwapchainRecreated()
{
    ImageLayouts.assign(Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
}

void Renderer::RecordMainColorPhase(const VulkanFrame& frame)
{
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (frame.ImageIndex < ImageLayouts.size())
    {
        oldLayout = ImageLayouts[frame.ImageIndex];
    }

    VulkanBarriers::TransitionForColorAttachment(
        frame.CommandBuffer, frame.SwapchainImage, oldLayout);

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = frame.SwapchainImageView;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.clearValue.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = frame.SwapchainExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttach;

    vkCmdBeginRendering(frame.CommandBuffer, &renderingInfo);

    FrameContext ctx;
    ctx.Cmd = frame.CommandBuffer;
    ctx.FrameInFlightIndex = frame.FrameIndex;
    ctx.TargetExtent = frame.SwapchainExtent;
    ctx.TargetFormat = frame.SwapchainFormat;
    ctx.Phase = RenderPhase::MainColor;

    for (IRenderFeature* feat : PhaseBuckets[static_cast<size_t>(RenderPhase::MainColor)])
    {
        feat->OnDraw(ctx);
    }

    vkCmdEndRendering(frame.CommandBuffer);

    VulkanBarriers::TransitionFromColorAttachmentToPresent(
        frame.CommandBuffer, frame.SwapchainImage);

    if (frame.ImageIndex < ImageLayouts.size())
    {
        ImageLayouts[frame.ImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
}
