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
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <graphics/vulkan/GpuTimestampPool.h>
#include <graphics/vulkan/VulkanDebugLabels.h>
#endif

#include <chrono>

namespace
{
    using RendererClock = std::chrono::steady_clock;

    double SecondsSince(RendererClock::time_point start)
    {
        return std::chrono::duration<double>(RendererClock::now() - start).count();
    }
}

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
    ImageCapture.Setup(Services);
    Valid = true;
}

Renderer::~Renderer()
{
    // Tear features down before any Vulkan service in our dependency list
    // starts unwinding. Order matters: features hold handles into the caches,
    // the caches own the VkDevice objects, and the device service outlives us.
    // Features destroy some Vulkan objects directly (descriptor pools,
    // samplers), so no submitted frame may still be executing when they do.
    if (Services.Device != nullptr && Services.Device->GetDevice() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Services.Device->GetDevice());
    // After the wait, so a capture recorded on the last frame still reaches
    // disk instead of being dropped on the way out.
    ImageCapture.Teardown();
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

    if (!feature->Setup(Services))
    {
        Log.Error("Renderer::AddFeature: feature setup failed; not registered");
        feature->Teardown();
        return nullptr;
    }

    IRenderFeature* raw = feature.get();
    PhaseBuckets[phaseIdx].push_back(raw);
    OwnedFeatures.push_back(std::move(feature));
    return raw;
}

RenderFrameResult Renderer::DrawFrameScheduled()
{
    if (!Valid) return RenderFrameResult::Failed;

    LastTiming = {};
    const auto totalStart = RendererClock::now();
    VulkanFrame frame;
    const VulkanFrameStatus begin = Frames.BeginFrame(frame);
    if (begin == VulkanFrameStatus::SwapchainOutOfDate
        || begin == VulkanFrameStatus::SurfaceSuboptimal
        || begin == VulkanFrameStatus::SurfaceUnavailable)
    {
        if (begin == VulkanFrameStatus::SurfaceUnavailable)
            return RenderFrameResult::SkippedMinimized;
        return begin == VulkanFrameStatus::SurfaceSuboptimal
            ? RenderFrameResult::SurfaceSuboptimal
            : RenderFrameResult::SwapchainOutOfDate;
    }
    if (begin != VulkanFrameStatus::Ready)
    {
        return RenderFrameResult::Failed;
    }

    // Rotate the per-frame scratch allocator into this frame's slice before
    // any feature draws -- feature code allocates transient UBOs from it.
    Services.Scratch->BeginFrame();

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* gpuScopes = Services.Instrumentation != nullptr
        ? Services.Instrumentation->GpuTimestamps
        : nullptr;
    if (gpuScopes != nullptr)
        gpuScopes->BeginFrame(frame.CommandBuffer, frame.FrameIndex);
#endif

    const auto recordStart = RendererClock::now();
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        VulkanDebugLabels::BeginLabel(frame.CommandBuffer,
                                      ToString(GpuScope::PhaseOffscreen));
        gpuScopes->BeginScope(frame.CommandBuffer, GpuScope::PhaseOffscreen);
    }
#endif
    RecordOffscreenPhase(frame);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.CommandBuffer, GpuScope::PhaseOffscreen);
        VulkanDebugLabels::EndLabel(frame.CommandBuffer);
        VulkanDebugLabels::BeginLabel(frame.CommandBuffer,
                                      ToString(GpuScope::PhaseMainColor));
        gpuScopes->BeginScope(frame.CommandBuffer, GpuScope::PhaseMainColor);
    }
#endif
    RecordMainColorPhase(frame);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.CommandBuffer, GpuScope::PhaseMainColor);
        VulkanDebugLabels::EndLabel(frame.CommandBuffer);
    }
#endif
    LastTiming.RecordSeconds = SecondsSince(recordStart);

    if (Services.Instrumentation != nullptr
        && Services.Instrumentation->Stats != nullptr)
    {
        RenderStats& stats = *Services.Instrumentation->Stats;
        stats.ScratchHighWaterBytes = Services.Scratch->GetHighWaterBytes();
        stats.ScratchUsedBytes = Services.Scratch->GetUsedBytes();
        stats.ScratchBytesPerFrame = Services.Scratch->GetBytesPerFrame();
        stats.ScratchAllocFailures = Services.Scratch->GetFailedAllocationCount();
    }

    // After EndFrame, so the frame that carried the copy has been submitted and
    // the retirement clock can start reporting it done.
    ImageCapture.Drain(Frames.GetRetirement());
    ++FramesDrawn;

    const VulkanFrameStatus end = Frames.EndFrame(frame);
    LastTiming.TotalSeconds = SecondsSince(totalStart);
    if (end == VulkanFrameStatus::SwapchainOutOfDate
        || end == VulkanFrameStatus::SurfaceSuboptimal
        || end == VulkanFrameStatus::SurfaceUnavailable)
    {
        if (end == VulkanFrameStatus::SurfaceUnavailable)
            return RenderFrameResult::SkippedMinimized;
        return end == VulkanFrameStatus::SurfaceSuboptimal
            ? RenderFrameResult::SurfaceSuboptimal
            : RenderFrameResult::SwapchainOutOfDate;
    }
    if (end != VulkanFrameStatus::Ready)
    {
        return RenderFrameResult::Failed;
    }
    return RenderFrameResult::Presented;
}

FrameContext Renderer::MakeCaptureContext(const VulkanFrame& frame) const
{
    FrameContext context;
    context.Cmd = frame.CommandBuffer;
    context.FrameInFlightIndex = frame.FrameIndex;
    context.TargetExtent = frame.SwapchainExtent;
    context.TargetFormat = frame.SwapchainFormat;
    context.Phase = RenderPhase::MainColor;
    context.Retirement = Frames.GetRetirement();
    return context;
}

bool Renderer::CaptureFrame(std::string path, std::uint64_t atFrame)
{
    if (!Swapchain.AreImagesCapturable())
        return false;
    ImageCapture.Request(std::move(path), atFrame);
    return true;
}

void Renderer::NotifySwapchainRecreated()
{
    ImageLayouts.assign(Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
    DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void Renderer::RecordOffscreenPhase(const VulkanFrame& frame)
{
    auto& bucket = PhaseBuckets[static_cast<size_t>(RenderPhase::Offscreen)];
    if (bucket.empty())
        return; // the runtime registers no offscreen features: nothing to do

    // No swapchain rendering scope is opened here. Each offscreen feature owns its
    // own render passes, targets, and image barriers.
    FrameContext ctx;
    ctx.Cmd = frame.CommandBuffer;
    ctx.FrameInFlightIndex = frame.FrameIndex;
    ctx.TargetExtent = frame.SwapchainExtent;
    ctx.Phase = RenderPhase::Offscreen;
    ctx.Retirement = Frames.GetRetirement();

    for (IRenderFeature* feat : bucket)
        feat->OnDraw(ctx);
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
        // One depth image serves every frame in flight, and a frame only
        // waits on the fence of the frame two slots back, so this barrier is
        // what orders these depth writes after the previous frame's. That
        // needs a first scope naming those writes: TOP_OF_PIPE names no
        // stage, which would leave the dependency empty.
        t.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                   | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                   | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.SrcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
    ctx.Retirement = Frames.GetRetirement();

    for (IRenderFeature* feat : PhaseBuckets[static_cast<size_t>(RenderPhase::MainColor)])
    {
        feat->OnDraw(ctx);
    }

    vkCmdEndRendering(frame.CommandBuffer);

    // Before the present transition, with the image still a colour attachment:
    // this is the finished frame, and capture leaves the layout as it found it.
    ImageCapture.Record(MakeCaptureContext(frame), FramesDrawn, frame.SwapchainImage,
                        frame.SwapchainExtent, frame.SwapchainFormat);

    VulkanBarriers::TransitionFromColorAttachmentToPresent(
        frame.CommandBuffer, frame.SwapchainImage);

    if (frame.ImageIndex < ImageLayouts.size())
    {
        ImageLayouts[frame.ImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
}
