#include <graphics/vulkan/Renderer.h>

#include <graphics/vulkan/PresentationFramePlan.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDepthTarget.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/GpuFrameScratch.h>
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

#include <algorithm>
#include <chrono>

// The neutral frame view over a backend FrameContext. The context outlives
// the OnDraw call it is projected for, so the Backend pointer is safe for
// exactly as long as the feature holds the frame -- the same lifetime the
// context reference had.
[[nodiscard]] static RenderFrame MakeRenderFrame(
    const FrameContext& ctx, const RenderInstrumentation* instrumentation)
{
    RenderFrame frame;
    frame.FrameInFlightIndex = ctx.FrameInFlightIndex;
    frame.TargetExtent = { ctx.TargetExtent.width, ctx.TargetExtent.height };
    frame.Phase = ctx.Phase;
    frame.Retirement = ctx.Retirement;
    frame.Instrumentation = instrumentation;
    frame.Backend = &ctx;
    return frame;
}

void BeginGpuScope(const RenderFrame& frame, GpuScope scope)
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* pool = frame.Instrumentation != nullptr
        ? frame.Instrumentation->GpuTimestamps
        : nullptr;
    if (pool == nullptr || frame.Backend == nullptr)
        return;
    VulkanDebugLabels::BeginLabel(frame.Backend->Cmd, ToString(scope));
    pool->BeginScope(frame.Backend->Cmd, scope);
#else
    (void)frame;
    (void)scope;
#endif
}

void EndGpuScope(const RenderFrame& frame, GpuScope scope)
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* pool = frame.Instrumentation != nullptr
        ? frame.Instrumentation->GpuTimestamps
        : nullptr;
    if (pool == nullptr || frame.Backend == nullptr)
        return;
    pool->EndScope(frame.Backend->Cmd, scope);
    VulkanDebugLabels::EndLabel(frame.Backend->Cmd);
#else
    (void)frame;
    (void)scope;
#endif
}

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
                   VulkanFrameService& frames,
                   VulkanAllocatorService& allocator,
                   VulkanBufferService& buffers,
                   VulkanImageService& images,
                   VulkanSamplerCache& samplers,
                   VulkanShaderCache& shaders,
                   VulkanPipelineCache& pipelines,
                   VulkanDescriptorCache& descriptors,
                   GpuFrameScratch& scratch,
                   VulkanUploadContextService& upload)
    : Log(logging.GetLogger<Renderer>())
    , Frames(frames)
{
    if (!device.IsValid() || !physicalDevice.IsValid() || !queues.IsValid()
        || !frames.IsValid() || !allocator.IsValid()
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
    Services.Swapchain = &frames.PrimarySwapchain();
    Services.Allocator = &allocator;
    Services.Buffers = &buffers;
    Services.Images = &images;
    Services.Samplers = &samplers;
    Services.Shaders = &shaders;
    Services.Pipelines = &pipelines;
    Services.Descriptors = &descriptors;
    Services.Scratch = &scratch;
    Services.Upload = &upload;

    // Binding 0 names the scratch ring for the rest of the cache's life. Passes
    // then declare only the range their block needs, so no pass can point the
    // shared binding somewhere its peers do not expect.
    descriptors.SetFrameUniformBuffer(scratch.GetBuffer());

    // The device's depth format, which offscreen targets use too; the primary
    // presentation's swapchain scope binds one of it.
    const VulkanDepthTarget depthProbe(images, physicalDevice);
    Services.DepthFormat = depthProbe.GetFormat();
    Services.StencilFormat = depthProbe.HasStencil() ? depthProbe.GetFormat() : VK_FORMAT_UNDEFINED;
    PresentationTarget& primary = *frames.FindPresentation(frames.PrimaryPresentation());
    primary.SwapchainFeatureServices = MakeSwapchainServices(primary);
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
    // Reverse registration order, which after resolution is reverse dependency
    // order: a feature is torn down before anything it depends on. The editor
    // needs exactly that -- its render feature frees ImGui descriptor sets
    // through the backend the UI feature owns.
    for (auto it = OwnedFeatures.rbegin(); it != OwnedFeatures.rend(); ++it)
    {
        if (*it) (*it)->Teardown();
    }
    OwnedFeatures.clear();
    RegisteredOrder.clear();
    for (auto& bucket : PhaseBuckets)
    {
        bucket.clear();
    }
}

IRenderFeature* Renderer::StageFeatureImpl(std::unique_ptr<IRenderFeature> feature,
                                           const FeatureRegistration& registration)
{
    if (!Valid || feature == nullptr) return nullptr;
    IRenderFeature* raw = feature.get();
    StagedFeatures.push_back(std::move(feature));
    StagedRegistrations.push_back(registration);
    return raw;
}

bool Renderer::CommitStagedFeatures(std::vector<std::string_view>* failedIds)
{
    if (failedIds != nullptr)
        failedIds->clear();
    if (StagedFeatures.empty())
        return true;

    std::vector<std::string_view> registeredIds;
    registeredIds.reserve(RegisteredOrder.size());
    for (const FeatureRegistration& entry : RegisteredOrder)
        registeredIds.push_back(entry.Id);

    std::vector<std::size_t> order;
    std::vector<FeatureOrderProblem> problems;
    if (!ResolveFeatureOrder(StagedRegistrations, registeredIds, order, problems))
    {
        for (const FeatureOrderProblem& problem : problems)
        {
            if (problem.Fault == FeatureOrderFault::UnknownDependency)
            {
                Log.Error("Renderer feature '{}' declares dependency '{}', which is "
                          "neither staged nor registered; the batch is refused",
                          problem.Id, problem.Dependency);
            }
            else
            {
                Log.Error("Renderer feature '{}': {}; the batch is refused",
                          problem.Id, ToString(problem.Fault));
            }
        }
        // Every staged id is reported, not none: the batch failing as a whole
        // destroys all of them, so a host reading only the list would see an
        // empty one and keep pointers to features that no longer exist.
        if (failedIds != nullptr)
            for (const FeatureRegistration& registration : StagedRegistrations)
                failedIds->push_back(registration.Id);
        StagedFeatures.clear();
        StagedRegistrations.clear();
        return false;
    }

    // Setup runs here, in resolved order: one feature's Setup may build the
    // state another's reads, which is exactly what the declared edges encode.
    std::vector<std::string_view> failed;
    for (const std::size_t index : order)
    {
        const FeatureRegistration& registration = StagedRegistrations[index];
        const bool dependencyFailed = std::any_of(
            registration.DependsOn.begin(), registration.DependsOn.end(),
            [&](std::string_view dependency)
            {
                return std::find(failed.begin(), failed.end(), dependency) != failed.end();
            });
        if (dependencyFailed)
        {
            // Skipped rather than set up against something that is not there:
            // a consumer whose producer failed would read state nobody built.
            Log.Error("Renderer feature '{}' skipped: a feature it depends on "
                      "failed to set up", registration.Id);
            failed.push_back(registration.Id);
            continue;
        }
        if (AddFeatureImpl(std::move(StagedFeatures[index]), registration) == nullptr)
            failed.push_back(registration.Id);
    }

    StagedFeatures.clear();
    StagedRegistrations.clear();
    if (failedIds != nullptr)
        *failedIds = failed;
    return failed.empty();
}

bool Renderer::RemoveFeature(IRenderFeature* feature)
{
    if (!Valid || feature == nullptr) return false;

    const auto owned = std::find_if(
        OwnedFeatures.begin(), OwnedFeatures.end(),
        [feature](const std::unique_ptr<IRenderFeature>& held)
        { return held.get() == feature; });
    if (owned == OwnedFeatures.end())
        return false;

    const auto index = static_cast<std::size_t>(
        std::distance(OwnedFeatures.begin(), owned));
    const FeatureRegistration registration = RegisteredOrder[index];

    // Everything except the feature being removed, so a feature does not block
    // its own removal by depending on something.
    std::vector<FeatureRegistration> others;
    others.reserve(RegisteredOrder.size());
    for (std::size_t i = 0; i < RegisteredOrder.size(); ++i)
    {
        if (i != index)
            others.push_back(RegisteredOrder[i]);
    }
    if (const std::string_view dependent = FindDependent(others, registration.Id);
        !dependent.empty())
    {
        Log.Error("Renderer feature '{}' cannot be removed: '{}' depends on it",
                  registration.Id, dependent);
        return false;
    }

    // The feature is about to destroy Vulkan objects it owns, and frames that
    // recorded it may still be executing.
    if (Services.Device != nullptr && Services.Device->GetDevice() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Services.Device->GetDevice());

    feature->Teardown();
    for (auto& bucket : PhaseBuckets)
    {
        std::erase_if(bucket, [feature](const BucketEntry& entry) { return entry.Feature == feature; });
    }
    OwnedFeatures.erase(owned);
    RegisteredOrder.erase(RegisteredOrder.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

IRenderFeature* Renderer::AddFeatureImpl(std::unique_ptr<IRenderFeature> feature,
                                         const FeatureRegistration& registration)
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

    RenderFeatureScope scope;
    PresentationTarget* presentation = registration.Scope.Kind == RenderFeatureScopeKind::Presentation
        ? Frames.FindPresentation(registration.Scope.Presentation)
        : nullptr;
    const FeatureScopeFault fault = ResolveFeatureScope(phase, registration.Scope, Frames.PrimaryPresentation(),
                                                        presentation != nullptr, scope);
    if (fault != FeatureScopeFault::None)
    {
        Log.Error("Renderer::AddFeature: feature '{}' {}; not registered", registration.Id,
                  fault == FeatureScopeFault::SwapchainPhaseNeedsPresentation
                      ? "records in a swapchain phase but is scoped Global"
                      : "names a presentation that does not exist");
        return nullptr;
    }

    // A swapchain-phase feature is set up against its presentation's
    // swapchain and attachment formats; everything else sees the device's.
    const RendererServices* backend = &Services;
    if (phase != RenderPhase::Offscreen)
    {
        PresentationTarget& target = *Frames.FindPresentation(scope.Presentation);
        target.SwapchainFeatureServices = MakeSwapchainServices(target);
        backend = &target.SwapchainFeatureServices;
    }

    RenderFeatureServices featureServices;
    featureServices.Logging = Services.Logging;
    featureServices.Instrumentation = Services.Instrumentation;
    featureServices.Buffers = GpuBuffers{Services.Buffers};
    featureServices.Images = GpuImages{Services.Images};
    featureServices.Scratch = Services.Scratch;
    featureServices.Backend = backend;
    if (!feature->Setup(featureServices))
    {
        Log.Error("Renderer::AddFeature: feature setup failed; not registered");
        feature->Teardown();
        return nullptr;
    }

    IRenderFeature* raw = feature.get();
    PhaseBuckets[phaseIdx].push_back(BucketEntry{ raw, scope });
    OwnedFeatures.push_back(std::move(feature));
    FeatureRegistration registered = registration;
    registered.Scope = scope;
    RegisteredOrder.push_back(registered);
    return raw;
}

RenderFrameResult Renderer::DrawFrameScheduled()
{
    if (!Valid) return RenderFrameResult::Failed;

    LastTiming = {};
    const auto totalStart = RendererClock::now();
    VulkanFrame frame;
    const VulkanFrameStatus begin = Frames.BeginFrame(frame);
    if (begin == VulkanFrameStatus::NothingAcquired)
        return SummarizeFrame(frame.PrimaryAcquire, 0);
    if (begin != VulkanFrameStatus::Ready)
        return RenderFrameResult::Failed;

    // Rotate the per-frame scratch allocator into this frame's slice before
    // any feature draws -- feature code allocates transient UBOs from it.
    Services.Scratch->BeginFrame();

    // The descriptor cache holds released bindless slots until the GPU proves
    // it is done with the frames that could still resolve them; this is where
    // it learns how far that proof has advanced.
    Services.Descriptors->BeginFrame(Frames.GetRetirement());

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
    }
#endif
    // The three swapchain phases share one rendering scope per presentation,
    // so each is labelled and timed inside rather than wrapped as a group here.
    for (const AcquiredPresentation& presentation : frame.Presentations)
        RecordSwapchainPhases(frame, presentation);
    LastTiming.RecordSeconds = SecondsSince(recordStart);

    if (Services.Instrumentation != nullptr
        && Services.Instrumentation->Stats != nullptr)
    {
        RenderStats& stats = *Services.Instrumentation->Stats;
        stats.ScratchHighWaterBytes = Services.Scratch->GetHighWaterBytes();
        stats.ScratchUsedBytes = Services.Scratch->GetUsedBytes();
        stats.ScratchBytesPerFrame = Services.Scratch->GetBytesPerFrame();
        stats.ScratchAllocFailures = Services.Scratch->GetFailedAllocationCount();
        const ScratchTagCounters& tags = Services.Scratch->GetTagCounters();
        for (std::size_t i = 0; i < ScratchTagCounters::kTagCount; ++i)
        {
            const auto tag = static_cast<ScratchTag>(i);
            stats.ScratchTagHighWaterBytes[i] = tags.HighWaterBytes(tag);
            stats.ScratchTagUsedBytes[i] = tags.UsedBytes(tag);
            stats.ScratchTagFailures[i] = tags.FailedAllocations(tag);
        }
    }

    // Before EndFrame, and deliberately: the retirement clock only advances in
    // BeginFrame, so a capture recorded this frame cannot retire until a later
    // one proves its fence either way. Draining here also covers the early
    // returns below, where a resize or a suboptimal swapchain would otherwise
    // hold a finished capture back a frame.
    ImageCapture.Drain(Frames.GetRetirement());
    ++FramesDrawn;

    const std::size_t recorded = frame.Presentations.size();
    const SurfaceOutcome end = Frames.EndFrame(frame);
    LastTiming.TotalSeconds = SecondsSince(totalStart);
    return SummarizeFrame(end, recorded);
}

FrameContext Renderer::MakeCaptureContext(const VulkanFrame& frame,
                                          const AcquiredPresentation& presentation) const
{
    FrameContext context;
    context.Cmd = frame.CommandBuffer;
    context.FrameInFlightIndex = frame.FrameIndex;
    context.TargetExtent = presentation.Extent;
    context.TargetFormat = presentation.Format;
    context.Phase = RenderPhase::MainColor;
    context.Retirement = Frames.GetRetirement();
    return context;
}

RendererServices Renderer::MakeSwapchainServices(const PresentationTarget& target) const
{
    RendererServices services = Services;
    services.Swapchain = &target.Swapchain();
    if (!target.Desc().DepthStencil)
    {
        services.DepthFormat = VK_FORMAT_UNDEFINED;
        services.StencilFormat = VK_FORMAT_UNDEFINED;
    }
    return services;
}

bool Renderer::WasAcquired(const VulkanFrame& frame, PresentationId id) const
{
    return std::ranges::any_of(frame.Presentations,
                               [id](const AcquiredPresentation& presentation) { return presentation.Id == id; });
}

PresentationId Renderer::CreatePresentation(SdlWindow& window, const PresentationDesc& desc)
{
    if (!Valid)
        return {};
    const PresentationId id = Frames.CreatePresentation(window, desc);
    if (PresentationTarget* target = Frames.FindPresentation(id))
        target->SwapchainFeatureServices = MakeSwapchainServices(*target);
    return id;
}

bool Renderer::DestroyPresentation(PresentationId id, std::function<void()> afterRetired)
{
    std::vector<RenderFeatureScope> scopes;
    scopes.reserve(RegisteredOrder.size());
    for (const FeatureRegistration& registration : RegisteredOrder)
        scopes.push_back(registration.Scope);
    if (IsPresentationBound(scopes, id))
    {
        Log.Error("Renderer: a presentation cannot be destroyed while a feature records into it");
        return false;
    }
    if (CapturePresentation == id)
        CapturePresentation = {};
    return Frames.RetirePresentation(id, std::move(afterRetired));
}

bool Renderer::CaptureFrame(std::string path, std::uint64_t atFrame)
{
    return CaptureFrame(Frames.PrimaryPresentation(), std::move(path), atFrame);
}

bool Renderer::CaptureFrame(PresentationId presentation, std::string path, std::uint64_t atFrame)
{
    const PresentationTarget* target = Frames.FindPresentation(presentation);
    if (target == nullptr || !target->Swapchain().AreImagesCapturable())
        return false;
    CapturePresentation = presentation;
    ImageCapture.Request(std::move(path), atFrame);
    return true;
}

void Renderer::RecordOffscreenPhase(const VulkanFrame& frame)
{
    auto& bucket = PhaseBuckets[static_cast<size_t>(RenderPhase::Offscreen)];
    if (bucket.empty())
        return; // nothing registered in this phase: no work to record

    // No swapchain rendering scope is opened here. Each offscreen feature owns its
    // own render passes, targets, and image barriers.
    FrameContext ctx;
    ctx.Cmd = frame.CommandBuffer;
    ctx.FrameInFlightIndex = frame.FrameIndex;
    ctx.Phase = RenderPhase::Offscreen;
    ctx.Retirement = Frames.GetRetirement();

    for (const BucketEntry& entry : bucket)
    {
        // A presentation that is not taking part this frame costs its
        // features nothing.
        ctx.TargetExtent = Services.Swapchain->GetExtent();
        if (entry.Scope.Kind == RenderFeatureScopeKind::Presentation)
        {
            if (!WasAcquired(frame, entry.Scope.Presentation))
                continue;
            ctx.TargetExtent = Frames.FindPresentation(entry.Scope.Presentation)->Swapchain().GetExtent();
        }
        entry.Feature->OnDraw(MakeRenderFrame(ctx, Services.Instrumentation));
    }
}

// The swapchain phases of one presentation, in one rendering scope. MainColor
// is the scene; ApplicationUi is authored user-facing UI drawn over it;
// DevelopmentOverlay is diagnostics drawn over everything. One
// vkCmdBeginRendering serves all three: the UI phases want the same colour
// attachment and no depth interaction, and their pipelines disable depth test
// and write rather than open a scope of their own.
//
// Capture and the present transition stay after the last bucket, so a capture is
// still the finished frame.
void Renderer::RecordSwapchainPhases(const VulkanFrame& frame, const AcquiredPresentation& presentation)
{
    PresentationTarget& target = *presentation.Target;
    PresentationTarget::ImageState& image = *target.Image(presentation.ImageIndex);

    VulkanBarriers::TransitionForColorAttachment(frame.CommandBuffer, presentation.Image, image.Layout);

    // Made the first frame this scope records, and remade on a resize.
    VulkanDepthTarget* depth = target.Depth();
    if (depth != nullptr)
    {
        const VkExtent2D oldDepthExtent = depth->GetExtent();
        depth->Recreate(presentation.Extent);
        if (oldDepthExtent.width != presentation.Extent.width
            || oldDepthExtent.height != presentation.Extent.height)
        {
            target.DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }
    // One layout for the whole attachment, decided by whether it carries a
    // stencil. A barrier whose aspect mask includes stencil may not use a
    // depth-only layout, and a rendering scope's attachments have to agree with
    // the layout the image is actually in -- so the barrier, the depth
    // attachment and the stencil attachment all read from here.
    const bool depthHasStencil = depth != nullptr && depth->HasStencil();
    const VkImageLayout depthLayout = depthHasStencil
        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    const VkImageView depthView = depth != nullptr ? depth->GetView() : VK_NULL_HANDLE;

    if (depth != nullptr && depth->GetImage() != VK_NULL_HANDLE)
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = depth->GetImage();
        t.OldLayout = target.DepthLayout;
        t.NewLayout = depthLayout;
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
        t.AspectMask = depthHasStencil
            ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            : VK_IMAGE_ASPECT_DEPTH_BIT;
        VulkanBarriers::TransitionImage(frame.CommandBuffer, t);
        target.DepthLayout = depthLayout;
    }

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = presentation.View;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.clearValue.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };

    VkRenderingAttachmentInfo depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttach.imageView = depthView;
    depthAttach.imageLayout = depthLayout;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.clearValue.depthStencil = { 1.0f, 0 };

    // The same view, as Vulkan requires when a scope binds both. Cleared to
    // zero so authored UI's clip mask starts from a known state rather than
    // from whatever the previous frame left; nothing else in the scope tests
    // against it.
    const bool hasStencil = depthHasStencil && depthView != VK_NULL_HANDLE;
    VkRenderingAttachmentInfo stencilAttach = depthAttach;
    stencilAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    stencilAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = presentation.Extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttach;
    renderingInfo.pDepthAttachment = depthView != VK_NULL_HANDLE ? &depthAttach : nullptr;
    renderingInfo.pStencilAttachment = hasStencil ? &stencilAttach : nullptr;

    vkCmdBeginRendering(frame.CommandBuffer, &renderingInfo);

    FrameContext ctx;
    ctx.Cmd = frame.CommandBuffer;
    ctx.FrameInFlightIndex = frame.FrameIndex;
    ctx.TargetExtent = presentation.Extent;
    ctx.TargetFormat = presentation.Format;
    ctx.DepthView = depthView;
    ctx.DepthFormat = depth != nullptr ? depth->GetFormat() : VK_FORMAT_UNDEFINED;
    ctx.StencilFormat = hasStencil ? depth->GetFormat() : VK_FORMAT_UNDEFINED;
    ctx.Retirement = Frames.GetRetirement();

    struct SwapchainPhase { RenderPhase Phase; GpuScope Scope; };
    constexpr SwapchainPhase kSwapchainPhases[] = {
        { RenderPhase::MainColor,          GpuScope::PhaseMainColor },
        { RenderPhase::ApplicationUi,      GpuScope::PhaseApplicationUi },
        { RenderPhase::DevelopmentOverlay, GpuScope::PhaseDevelopmentOverlay },
    };
    // Phase timings are the primary's: a scope is one entry per frame.
    [[maybe_unused]] const bool timed = presentation.Id == Frames.PrimaryPresentation();

    for (const auto& [phase, scope] : kSwapchainPhases)
    {
        auto& bucket = PhaseBuckets[static_cast<size_t>(phase)];
        if (bucket.empty())
            continue;

#ifdef SENCHA_ENABLE_RENDER_PROFILING
        GpuTimestampPool* const phaseScopes = timed && Services.Instrumentation != nullptr
            ? Services.Instrumentation->GpuTimestamps
            : nullptr;
        if (phaseScopes != nullptr)
        {
            VulkanDebugLabels::BeginLabel(frame.CommandBuffer, ToString(scope));
            phaseScopes->BeginScope(frame.CommandBuffer, scope);
        }
#endif
        ctx.Phase = phase;
        for (const BucketEntry& entry : bucket)
        {
            if (entry.Scope.Presentation == presentation.Id)
                entry.Feature->OnDraw(MakeRenderFrame(ctx, Services.Instrumentation));
        }
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        if (phaseScopes != nullptr)
        {
            phaseScopes->EndScope(frame.CommandBuffer, scope);
            VulkanDebugLabels::EndLabel(frame.CommandBuffer);
        }
#endif
    }

    vkCmdEndRendering(frame.CommandBuffer);

    // Before the present transition, with the image still a colour attachment:
    // this is the finished frame, and capture leaves the layout as it found it.
    if (presentation.Id == CapturePresentation)
        ImageCapture.Record(MakeCaptureContext(frame, presentation), FramesDrawn, presentation.Image,
                            presentation.Extent, presentation.Format);

    VulkanBarriers::TransitionFromColorAttachmentToPresent(frame.CommandBuffer, presentation.Image);
    image.Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
