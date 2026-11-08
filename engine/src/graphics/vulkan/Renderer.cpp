#include <graphics/vulkan/Renderer.h>

#include <graphics/vulkan/PresentationFramePlan.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
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

    constexpr VkClearValue kSceneClear{ .color = { { 0.05f, 0.09f, 0.12f, 1.0f } } };

    // Times one phase of one presentation. Phase timings are the primary's: a
    // scope is one entry per frame.
    class PhaseTiming
    {
    public:
        PhaseTiming([[maybe_unused]] const RendererServices& services, [[maybe_unused]] VkCommandBuffer cmd,
                    [[maybe_unused]] GpuScope scope, [[maybe_unused]] bool primary)
        {
#ifdef SENCHA_ENABLE_RENDER_PROFILING
            Pool = primary && services.Instrumentation != nullptr ? services.Instrumentation->GpuTimestamps : nullptr;
            Cmd = cmd;
            Scope = scope;
            if (Pool != nullptr)
            {
                VulkanDebugLabels::BeginLabel(Cmd, ToString(Scope));
                Pool->BeginScope(Cmd, Scope);
            }
#endif
        }

        ~PhaseTiming()
        {
#ifdef SENCHA_ENABLE_RENDER_PROFILING
            if (Pool != nullptr)
            {
                Pool->EndScope(Cmd, Scope);
                VulkanDebugLabels::EndLabel(Cmd);
            }
#endif
        }

        PhaseTiming(const PhaseTiming&) = delete;
        PhaseTiming& operator=(const PhaseTiming&) = delete;

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    private:
        GpuTimestampPool* Pool = nullptr;
        VkCommandBuffer Cmd = VK_NULL_HANDLE;
        GpuScope Scope{};
#endif
    };

    // One presentation's depth, ready for the scope about to bind it.
    struct ScopeDepth
    {
        VkImageView View = VK_NULL_HANDLE;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        bool HasStencil = false;
    };

    // Made the first frame a scope binds it, and remade on a resize. Called before
    // each scope that binds the image, whatever it does with the contents: one
    // image serves every frame in flight and every scope in this one, and a frame
    // only waits on the fence of the frame two slots back, so this barrier is
    // what orders these accesses after the last writes. That needs a first scope
    // naming those writes: TOP_OF_PIPE names no stage, which would leave the
    // dependency empty.
    ScopeDepth PrepareScopeDepth(VkCommandBuffer cmd, PresentationTarget& target, VkExtent2D extent)
    {
        VulkanDepthTarget* depth = target.Depth();
        if (depth == nullptr)
            return {};

        const VkExtent2D oldExtent = depth->GetExtent();
        depth->Recreate(extent);
        if (oldExtent.width != extent.width || oldExtent.height != extent.height)
            target.DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // A rendering scope's attachments have to agree with the layout the image is
        // actually in, so the whole attachment takes the one DepthAttachmentLayout.
        const bool hasStencil = depth->HasStencil();
        if (depth->GetImage() != VK_NULL_HANDLE)
        {
            VulkanBarriers::OrderDepthAttachment(cmd, depth->GetImage(), hasStencil, target.DepthLayout);
            target.DepthLayout = VulkanBarriers::DepthAttachmentLayout(hasStencil);
        }
        return { depth->GetView(), depth->GetFormat(), hasStencil && depth->GetView() != VK_NULL_HANDLE };
    }

    void AttachDepth(RenderScopeDesc& desc, const ScopeDepth& depth, VkAttachmentLoadOp depthLoad,
                     VkAttachmentStoreOp depthStore)
    {
        desc.DepthFormat = depth.Format;
        if (depth.View == VK_NULL_HANDLE)
            return;
        VkClearValue clear{};
        clear.depthStencil = { 1.0f, 0 };
        desc.Depth = { depth.View, depthLoad, depthStore, clear };
        if (!depth.HasStencil)
            return;
        // The same view, as Vulkan requires when a scope binds both. Cleared to
        // zero so authored UI's clip mask starts from a known state rather than
        // from whatever the previous frame left; nothing else tests against it.
        desc.Stencil = { depth.View, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, clear };
        desc.StencilFormat = depth.Format;
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
    Services.Health = &FrameHealth;

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
    if (UiTextures.Setup(Services))
        Services.UiTextures = &UiTextures;
    Valid = true;
}

Renderer::~Renderer()
{
    // Tear features down before any Vulkan service in our dependency list
    // starts unwinding. Order matters: features hold handles into the caches,
    // the caches own the VkDevice objects, and the device service outlives us.
    // Features destroy some Vulkan objects directly (descriptor pools,
    // samplers), so no submitted frame may still be executing when they do.
    WaitForDeviceIdle();
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
    // After the features: a pass resolves through it until its own teardown.
    UiTextures.Teardown();
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

bool Renderer::CommitStagedFeatures()
{
    std::vector<std::unique_ptr<IRenderFeature>> features = std::move(StagedFeatures);
    const std::vector<FeatureRegistration> registrations = std::move(StagedRegistrations);
    StagedFeatures.clear();
    StagedRegistrations.clear();
    return CommitBatch(features, registrations);
}

bool Renderer::CommitBatch(std::vector<std::unique_ptr<IRenderFeature>>& features,
                           std::span<const FeatureRegistration> registrations)
{
    if (features.empty())
        return true;

    std::vector<std::string_view> registeredIds;
    registeredIds.reserve(RegisteredOrder.size());
    for (const FeatureRegistration& entry : RegisteredOrder)
        registeredIds.push_back(entry.Id);

    std::vector<std::size_t> order;
    std::vector<FeatureOrderProblem> problems;
    if (!ResolveFeatureOrder(registrations, registeredIds, order, problems))
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
        return false;
    }

    // Setup runs here, in resolved order: one feature's Setup may build the
    // state another's reads, which is exactly what the declared edges encode.
    const std::size_t firstOfBatch = OwnedFeatures.size();
    for (const std::size_t index : order)
    {
        if (SetUpAndRegister(std::move(features[index]), registrations[index]) == nullptr)
        {
            if (const std::size_t setUp = OwnedFeatures.size() - firstOfBatch; setUp != 0)
                Log.Error("Renderer feature '{}' did not register; the {} set up before it "
                          "in its batch are withdrawn", registrations[index].Id, setUp);
            TearDownRegisteredFrom(firstOfBatch);
            return false;
        }
    }
    return true;
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
    WaitForDeviceIdle();
    TearDownRegisteredAt(index);
    return true;
}

IRenderFeature* Renderer::AddFeatureImpl(std::unique_ptr<IRenderFeature> feature,
                                         const FeatureRegistration& registration)
{
    if (!Valid || feature == nullptr) return nullptr;
    if (registration.Id.empty() && registration.DependsOn.empty())
        return SetUpAndRegister(std::move(feature), registration);

    IRenderFeature* raw = feature.get();
    std::vector<std::unique_ptr<IRenderFeature>> batch;
    batch.push_back(std::move(feature));
    return CommitBatch(batch, std::span<const FeatureRegistration>(&registration, 1)) ? raw : nullptr;
}

IRenderFeature* Renderer::SetUpAndRegister(std::unique_ptr<IRenderFeature> feature,
                                           const FeatureRegistration& registration)
{
    const RenderPhase phase = feature->GetPhase();
    const size_t phaseIdx = static_cast<size_t>(phase);
    if (phaseIdx >= static_cast<size_t>(RenderPhase::Count))
    {
        Log.Error("Renderer feature reports invalid phase ({})",
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
        Log.Error("Renderer feature '{}' {}; not registered", registration.Id,
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
        Log.Error("Renderer feature '{}' setup failed; not registered", registration.Id);
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

void Renderer::TearDownRegisteredFrom(std::size_t first)
{
    // A Setup may have submitted uploads that are still executing.
    WaitForDeviceIdle();
    while (OwnedFeatures.size() > first)
        TearDownRegisteredAt(OwnedFeatures.size() - 1);
}

void Renderer::TearDownRegisteredAt(std::size_t index)
{
    IRenderFeature* feature = OwnedFeatures[index].get();
    feature->Teardown();
    for (auto& bucket : PhaseBuckets)
    {
        std::erase_if(bucket, [feature](const BucketEntry& entry) { return entry.Feature == feature; });
    }
    OwnedFeatures.erase(OwnedFeatures.begin() + static_cast<std::ptrdiff_t>(index));
    RegisteredOrder.erase(RegisteredOrder.begin() + static_cast<std::ptrdiff_t>(index));
}

void Renderer::WaitForDeviceIdle()
{
    if (Services.Device != nullptr && Services.Device->GetDevice() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Services.Device->GetDevice());
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
    FrameHealth = RenderFrameHealth{ .FramesRecorded = FramesDrawn + 1 };

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
    for (const AcquiredPresentation& presentation : frame.Presentations)
    {
        FrameContext context;
        context.Cmd = frame.CommandBuffer;
        context.FrameInFlightIndex = frame.FrameIndex;
        context.TargetExtent = presentation.Extent;
        context.TargetFormat = presentation.Format;
        context.Retirement = Frames.GetRetirement();
        ScenePostState post;
        context.ScenePost = &post;
        RecordScenePhase(context, presentation);
        RecordPostPhase(context, presentation);
        RecordSwapchainPhases(frame, context, presentation);
    }
    LastTiming.RecordSeconds = SecondsSince(recordStart);

    const ScratchTagCounters& tags = Services.Scratch->GetTagCounters();
    FrameHealth.ScratchFailures = Services.Scratch->GetFailedAllocationCount();
    for (std::size_t i = 0; i < ScratchTagCounters::kTagCount; ++i)
        FrameHealth.ScratchTagFailures[i] = tags.FailedAllocations(static_cast<ScratchTag>(i));

    if (Services.Instrumentation != nullptr
        && Services.Instrumentation->Stats != nullptr)
    {
        RenderStats& stats = *Services.Instrumentation->Stats;
        stats.ScratchHighWaterBytes = Services.Scratch->GetHighWaterBytes();
        stats.ScratchUsedBytes = Services.Scratch->GetUsedBytes();
        stats.ScratchBytesPerFrame = Services.Scratch->GetBytesPerFrame();
        stats.ScratchAllocFailures = Services.Scratch->GetFailedAllocationCount();
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

FrameContext SwapchainScopeFormats(const RendererServices& services)
{
    FrameContext formats;
    formats.TargetFormat =
        services.Swapchain != nullptr ? services.Swapchain->GetFormat() : VK_FORMAT_UNDEFINED;
    formats.DepthFormat = services.DepthFormat;
    formats.StencilFormat = services.StencilFormat;
    return formats;
}

FrameContext SceneScopeFormats(const RendererServices& services)
{
    FrameContext formats = SwapchainScopeFormats(services);
    formats.TargetFormat = kSceneColorFormat;
    return formats;
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

bool Renderer::HasFeatureFor(RenderPhase phase, PresentationId presentation) const
{
    return std::ranges::any_of(PhaseBuckets[static_cast<size_t>(phase)], [presentation](const BucketEntry& entry) {
        return entry.Scope.Presentation == presentation;
    });
}

void Renderer::RecordPresentationPhase(const FrameContext& context, RenderPhase phase,
                                       PresentationId presentation) const
{
    FrameContext phaseContext = context;
    phaseContext.Phase = phase;
    for (const BucketEntry& entry : PhaseBuckets[static_cast<size_t>(phase)])
    {
        if (entry.Scope.Presentation == presentation)
            entry.Feature->OnDraw(MakeRenderFrame(phaseContext, Services.Instrumentation));
    }
}

// The scene in its own target, with depth kept for the phases after it. A
// presentation with no MainColor feature, as an editor window is, has no target
// and pays for none. Docs: docs/renderer/frame.md.
void Renderer::RecordScenePhase(FrameContext& context, const AcquiredPresentation& presentation)
{
    if (!HasFeatureFor(RenderPhase::MainColor, presentation.Id))
        return;

    PresentationTarget& target = *presentation.Target;
    const std::optional<RenderTargetView> scene = target.AcquireSceneColor(Services, presentation.Extent);
    if (!scene.has_value())
    {
        Log.Error("Renderer: presentation has no scene target this frame; its scene is skipped");
        return;
    }
    const ScopeDepth depth = PrepareScopeDepth(context.Cmd, target, presentation.Extent);

    {
        RenderTargetSession session(context.Cmd, scene->ColorImage, *scene->ColorAccess);
        RenderScopeDesc desc;
        desc.Area.extent = presentation.Extent;
        desc.Color = { scene->ColorView, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, kSceneClear };
        desc.ColorFormat = kSceneColorFormat;
        desc.Phase = RenderPhase::MainColor;
        AttachDepth(desc, depth, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);

        RenderScope scope(context, desc);
        const PhaseTiming timing(Services, context.Cmd, GpuScope::PhaseMainColor,
                                 presentation.Id == Frames.PrimaryPresentation());
        RecordPresentationPhase(scope.Context(), RenderPhase::MainColor, presentation.Id);
    }

    context.SceneColorBindless = scene->BindlessIndex;
    context.SceneExtent = scene->Extent;
}

void Renderer::RecordPostPhase(const FrameContext& context, const AcquiredPresentation& presentation)
{
    if (!HasFeatureFor(RenderPhase::Post, presentation.Id))
        return;
    const PhaseTiming timing(Services, context.Cmd, GpuScope::PhasePost,
                             presentation.Id == Frames.PrimaryPresentation());
    // The scene's depth, ordered after its writes, for a feature that tests against it.
    const ScopeDepth depth = PrepareScopeDepth(context.Cmd, *presentation.Target, presentation.Extent);
    FrameContext post = context;
    post.DepthView = depth.View;
    post.DepthFormat = depth.Format;
    post.StencilFormat = depth.HasStencil ? depth.Format : VK_FORMAT_UNDEFINED;
    RecordPresentationPhase(post, RenderPhase::Post, presentation.Id);
}

// Output and the UI phases of one presentation, in one rendering scope. One
// vkCmdBeginRendering serves all three: they want the same colour attachment
// and no depth interaction, and their pipelines disable depth test and write
// rather than open a scope of their own.
//
// Capture and the present transition stay after the last bucket, so a capture is
// still the finished frame.
void Renderer::RecordSwapchainPhases(const VulkanFrame& frame, const FrameContext& context,
                                     const AcquiredPresentation& presentation)
{
    PresentationTarget& target = *presentation.Target;
    PresentationTarget::ImageState& image = *target.Image(presentation.ImageIndex);

    if (context.SceneColorBindless != UINT32_MAX && !HasFeatureFor(RenderPhase::Output, presentation.Id)
        && !target.ReportedNoOutputFeature)
    {
        target.ReportedNoOutputFeature = true;
        Log.Error("Renderer: a presentation draws a scene with no Output feature, so its window never shows it");
    }

    VulkanBarriers::TransitionForColorAttachment(frame.CommandBuffer, presentation.Image, image.Layout);
    const ScopeDepth depth = PrepareScopeDepth(frame.CommandBuffer, target, presentation.Extent);

    RenderScopeDesc desc;
    desc.Area.extent = presentation.Extent;
    desc.Color = { presentation.View, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, kSceneClear };
    desc.ColorFormat = presentation.Format;
    desc.Phase = RenderPhase::Output;
    // What the scene wrote, for a diagnostic drawn over it in the Output phase.
    AttachDepth(desc, depth,
                context.SceneColorBindless != UINT32_MAX ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_DONT_CARE);

    {
        RenderScope scope(context, desc);
        struct SwapchainPhase { RenderPhase Phase; GpuScope Scope; };
        constexpr SwapchainPhase kSwapchainPhases[] = {
            { RenderPhase::Output,             GpuScope::PhaseOutput },
            { RenderPhase::ApplicationUi,      GpuScope::PhaseApplicationUi },
            { RenderPhase::DevelopmentOverlay, GpuScope::PhaseDevelopmentOverlay },
        };
        for (const auto& [phase, gpuScope] : kSwapchainPhases)
        {
            if (PhaseBuckets[static_cast<size_t>(phase)].empty())
                continue;
            const PhaseTiming timing(Services, frame.CommandBuffer, gpuScope,
                                     presentation.Id == Frames.PrimaryPresentation());
            RecordPresentationPhase(scope.Context(), phase, presentation.Id);
        }
    }

    // Before the present transition, with the image still a colour attachment:
    // this is the finished frame, and capture leaves the layout as it found it.
    if (presentation.Id == CapturePresentation)
        ImageCapture.Record(MakeCaptureContext(frame, presentation), FramesDrawn, presentation.Image,
                            presentation.Extent, presentation.Format);

    VulkanBarriers::TransitionFromColorAttachmentToPresent(frame.CommandBuffer, presentation.Image);
    image.Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
