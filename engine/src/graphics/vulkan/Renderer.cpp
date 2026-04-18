#include <graphics/vulkan/Renderer.h>

#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDepthTarget.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>

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
    Services.Swapchain = &swapchain;
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
    DepthTarget = std::make_unique<VulkanDepthTarget>(images, physicalDevice);
    DepthTarget->Create(swapchain.GetExtent());
    Services.DepthFormat = DepthTarget->GetFormat();
    Valid = true;
}

Renderer::~Renderer()
{
    // Vacate all registry slots before any Teardown() runs. This provides
    // two-stage protection: Get() returns nullptr while the registry is still
    // alive (slots cleared), and after ~Renderer returns the shared_ptr drops,
    // so weak_ptr::lock() also fails for any refs that outlive the Renderer.
    FeatureRegistry->RemoveAll();

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

IRenderFeature* Renderer::AddFeatureImpl(std::unique_ptr<IRenderFeature> feature)
{
    if (!Valid || feature == nullptr) return nullptr;

    const RenderPhase phase = feature->GetPhase();
    const size_t phaseIdx = static_cast<size_t>(phase);
    if (phaseIdx >= static_cast<size_t>(RenderPhase::Count))
    {
        Log.Error("Renderer::AddFeature: feature reports invalid phase ({})",
                  static_cast<int>(phase));
        return nullptr;
    }

    feature->Setup(Services);

    IRenderFeature* raw = feature.get();
    PhaseBuckets[phaseIdx].push_back(raw);
    OwnedFeatures.push_back(std::move(feature));
    return raw;
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
    DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

    const VkExtent2D oldDepthExtent = DepthTarget->GetExtent();
    DepthTarget->Recreate(frame.SwapchainExtent);
    if (oldDepthExtent.width != frame.SwapchainExtent.width
        || oldDepthExtent.height != frame.SwapchainExtent.height)
    {
        DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    if (DepthTarget->GetImage() != VK_NULL_HANDLE)
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = DepthTarget->GetImage();
        t.OldLayout = DepthLayout;
        t.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                   | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VulkanBarriers::TransitionImage(frame.CommandBuffer, t);
        DepthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = frame.SwapchainImageView;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.clearValue.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };

    VkRenderingAttachmentInfo depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttach.imageView = DepthTarget->GetView();
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = frame.SwapchainExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttach;
    renderingInfo.pDepthAttachment = depthAttach.imageView != VK_NULL_HANDLE ? &depthAttach : nullptr;

    vkCmdBeginRendering(frame.CommandBuffer, &renderingInfo);

    FrameContext ctx;
    ctx.Cmd = frame.CommandBuffer;
    ctx.FrameInFlightIndex = frame.FrameIndex;
    ctx.TargetExtent = frame.SwapchainExtent;
    ctx.TargetFormat = frame.SwapchainFormat;
    ctx.DepthView = DepthTarget->GetView();
    ctx.DepthFormat = DepthTarget->GetFormat();
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
