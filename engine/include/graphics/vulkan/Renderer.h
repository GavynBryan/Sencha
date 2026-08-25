#pragma once

#include <string>

#include <core/logging/LoggingProvider.h>
#include <graphics/RenderFeature.h>
#include <graphics/vulkan/FeatureRegistrationOrder.h>
#include <graphics/vulkan/FrameImageCapture.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

class VulkanDeviceService;
class VulkanPhysicalDeviceService;
class VulkanQueueService;
class VulkanSwapchainService;
class VulkanAllocatorService;
class VulkanBufferService;
class VulkanImageService;
class VulkanSamplerCache;
class VulkanShaderCache;
class VulkanPipelineCache;
class VulkanDescriptorCache;
class GpuFrameScratch;
class VulkanUploadContextService;
class VulkanDepthTarget;
struct RenderInstrumentation;

//=============================================================================
// Renderer
//
// Top-level render facade. Owns an ordered list of IRenderFeature instances
// organized into phases and drives them through a single DrawFrame() call.
//
// Design constraints Sencha locks in at this layer:
//
//   * Features are owned by the Renderer (unique_ptr). Setup() runs once
//     at AddFeature time; Teardown() runs in the Renderer destructor before
//     any Vulkan service is torn down.
//
//   * The per-frame path is flat. OnDraw() receives a small, cache-dense
//     FrameContext and nothing else -- there are no service lookups in
//     the hot loop. Features are expected to cache direct pointers during
//     Setup() from the RendererServices bundle.
//
//   * Phase-aware from day one. Even with only MainColor implemented today,
//     every feature is bucketed by the phase it reports, so adding offscreen
//     / shadow / UI phases later doesn't churn the feature interface.
//
//   * Features are added after the device exists. GraphicsServices builds its
//     own VulkanBootstrapPolicy and brings up the Vulkan stack during
//     Engine::Initialize, which runs before any game hook that could
//     construct a feature; features are then handed over via AddFeature() and
//     acquire their GPU resources in Setup(). A feature that needs a device
//     extension or feature bit would need a hook at engine configuration
//     time, before the device is built -- there is no such hook today.
//=============================================================================

// RenderPhase, IRenderFeature, RenderFeatureServices, and RenderFrame live in
// graphics/RenderFeature.h -- the neutral feature contract. What follows here
// is the Vulkan side of it: the backend bundle behind
// RenderFeatureServices::Backend and the recording state behind
// RenderFrame::Backend.

// Direct service pointers behind the feature contract. Recording passes cache
// whichever ones they need at Setup and never reach for engine services again.
struct RendererServices
{
    LoggingProvider* Logging = nullptr;
    VulkanDeviceService* Device = nullptr;
    VulkanPhysicalDeviceService* PhysicalDevice = nullptr;
    VulkanQueueService* Queues = nullptr;
    VulkanSwapchainService* Swapchain = nullptr;
    VulkanAllocatorService* Allocator = nullptr;
    VulkanBufferService* Buffers = nullptr;
    VulkanImageService* Images = nullptr;
    VulkanSamplerCache* Samplers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    GpuFrameScratch* Scratch = nullptr;
    VulkanUploadContextService* Upload = nullptr;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    // The engine's instrumentation bundle. The pointer is stable for the
    // renderer's life; the members flip with render.profile.mode, so cache
    // the bundle and re-read its members per frame, never the members.
    const RenderInstrumentation* Instrumentation = nullptr;
};

// Small dense payload handed to OnDraw(). Everything a feature needs to
// record draws for one frame lives right here -- no pointer chases into
// the Renderer or the frame service.
struct FrameContext
{
    VkCommandBuffer Cmd = VK_NULL_HANDLE;
    uint32_t FrameInFlightIndex = 0;
    VkExtent2D TargetExtent{};
    VkFormat TargetFormat = VK_FORMAT_UNDEFINED;
    VkImageView DepthView = VK_NULL_HANDLE;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    RenderPhase Phase = RenderPhase::MainColor;
    // Fence-anchored frame clock. A feature releasing a GPU resource the
    // renderer may still be reading stamps it from here and frees it once this
    // reports it retired, rather than counting frames itself.
    GpuFrameRetirement Retirement;
};

struct RendererFrameTiming
{
    double RecordSeconds = 0.0;
    double TotalSeconds = 0.0;
};

class Renderer
{
public:
    Renderer(LoggingProvider& logging,
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
             GpuFrameScratch& scratch,
             VulkanUploadContextService& upload);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // Take ownership of a feature, run its Setup(), and return the raw
    // pointer (or nullptr on failure). The Renderer owns the feature for its
    // lifetime; callers that need to reach it later cache this pointer, but
    // features are expected to cache their own service pointers in Setup() and
    // are driven each frame through the Renderer's internal phase buckets.
    //
    // A feature with no declared dependencies: a batch of one, committed
    // immediately. Hosts whose features depend on each other stage them and
    // commit the batch instead.
    template <typename T>
    T* AddFeature(std::unique_ptr<T> feature)
    {
        static_assert(std::is_base_of_v<IRenderFeature, T>,
                      "T must derive from IRenderFeature");
        if (!Valid || !feature) return nullptr;
        return static_cast<T*>(
            AddFeatureImpl(std::unique_ptr<IRenderFeature>(feature.release()), {}));
    }

    // Hold a feature and its declared dependencies until CommitStagedFeatures.
    // Returns the pointer the feature will have if the batch commits, so a host
    // can wire panels and callbacks against it -- but the pointer is only good
    // once the commit reports the id succeeded.
    //
    // Setup does NOT run here: it runs at commit time, in resolved order, which
    // is what lets one feature's Setup depend on another's having already run.
    template <typename T>
    T* StageFeature(std::unique_ptr<T> feature, const FeatureRegistration& registration)
    {
        static_assert(std::is_base_of_v<IRenderFeature, T>,
                      "T must derive from IRenderFeature");
        if (!Valid || !feature) return nullptr;
        return static_cast<T*>(StageFeatureImpl(
            std::unique_ptr<IRenderFeature>(feature.release()), registration));
    }

    // Resolves the staged batch, runs each Setup in dependency order, and
    // registers what succeeded. Returns false when the batch as a whole could
    // not be ordered -- a duplicate id, an unknown dependency, or a cycle -- in
    // which case nothing is registered and the staged features are destroyed.
    //
    // A feature whose Setup returns false is skipped along with everything that
    // declared a dependency on it, and its id lands in `failedIds` if given, so
    // a host can null whatever it cached. That is degradation, not a broken
    // graph, so the rest of the batch still commits.
    bool CommitStagedFeatures(std::vector<std::string_view>* failedIds = nullptr);

    // Tears down a registered feature and drops it, with the GPU drained first.
    // Legal only for a feature nothing else declares a dependency on -- removing
    // a producer out from under its consumers is refused and logged.
    //
    // For a host that must release a feature while the state it borrows is
    // still alive: an editor's render feature holds meshes and materials its
    // asset system owns, and the renderer outlives both.
    bool RemoveFeature(IRenderFeature* feature);

    // Render frame scheduler entry point: acquire -> scratch rotate -> phase
    // iterate -> transition -> submit/present. Returns structured lifecycle
    // results so RuntimeFrameLoop can keep render instability out of game time.
    RenderFrameResult DrawFrameScheduled();

    [[nodiscard]] const RendererFrameTiming& GetLastTiming() const { return LastTiming; }

    // Reset per-swapchain-image tracking after VulkanSwapchainService::Recreate.
    void NotifySwapchainRecreated();

    // Installs the engine's instrumentation bundle. Must run before any
    // AddFeature so every feature Setup sees it in RendererServices.
    void SetInstrumentation(const RenderInstrumentation* instrumentation)
    {
        Services.Instrumentation = instrumentation;
    }

    // Write a presented frame to `path` as a PNG, once this renderer has drawn
    // `atFrame` of them. False when the surface did not offer readback usage,
    // in which case nothing is armed.
    [[nodiscard]] bool CaptureFrame(std::string path, std::uint64_t atFrame);

    // Frames this renderer has drawn. Monotonic and its own count: the loop
    // above it counts driven frames, which includes the ones that resized or
    // rebuilt a swapchain instead of rendering.
    [[nodiscard]] std::uint64_t GetFramesDrawn() const { return FramesDrawn; }

private:
    // The context handed to frame capture: the same command buffer and clock
    // the features saw, without the attachment fields, which describe a scope
    // that has already closed by the time the frame is copied.
    [[nodiscard]] FrameContext MakeCaptureContext(const VulkanFrame& frame) const;


    Logger& Log;
    VulkanSwapchainService& Swapchain;
    VulkanFrameService& Frames;
    RendererServices Services;
    bool Valid = false;

    std::vector<std::unique_ptr<IRenderFeature>> OwnedFeatures;
    std::vector<IRenderFeature*> PhaseBuckets[static_cast<size_t>(RenderPhase::Count)];
    // Parallel to OwnedFeatures: what each registered feature declared, which
    // is what a removal consults to refuse orphaning a consumer. Ids point at
    // string literals owned by the host.
    std::vector<FeatureRegistration> RegisteredOrder;
    // Staged but not yet committed, with the declarations they were staged with.
    std::vector<std::unique_ptr<IRenderFeature>> StagedFeatures;
    std::vector<FeatureRegistration> StagedRegistrations;
    std::vector<VkImageLayout> ImageLayouts;
    std::unique_ptr<VulkanDepthTarget> DepthTarget;
    VkImageLayout DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    RendererFrameTiming LastTiming;
    FrameImageCapture ImageCapture;
    std::uint64_t FramesDrawn = 0;

    IRenderFeature* StageFeatureImpl(std::unique_ptr<IRenderFeature> feature,
                                     const FeatureRegistration& registration);
    // Validates phase, runs Setup(), pushes into OwnedFeatures/PhaseBuckets.
    // Returns the raw pointer on success, nullptr on failure.
    IRenderFeature* AddFeatureImpl(std::unique_ptr<IRenderFeature> feature,
                                   const FeatureRegistration& registration);

    void RecordOffscreenPhase(const VulkanFrame& frame);
    void RecordMainColorPhase(const VulkanFrame& frame);
};
