#pragma once

#include <string>

#include <core/logging/LoggingProvider.h>
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

enum class RenderPhase : uint8_t
{
    // Features here own their render passes/targets (no swapchain pass is open) and
    // run before MainColor: e.g. the editor rendering viewports to offscreen textures.
    Offscreen = 0,
    MainColor = 1,
    // Reserved for: Shadow, Opaque, Transparent, UI, Post...
    Count
};

// Direct service pointers handed to features in Setup(). Features should
// cache whichever ones they need and never reach for engine services again.
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

class IRenderFeature
{
public:
    virtual ~IRenderFeature() = default;

    // Which phase this feature runs in. One feature, one phase.
    [[nodiscard]] virtual RenderPhase GetPhase() const = 0;

    // Runs once, inside Renderer::AddFeature. Cache service pointers here.
    // Do any up-front GPU resource creation here too. Returning false means
    // the feature is not usable: AddFeature tears it down and refuses to
    // register it, rather than leaving an inert feature in a phase bucket.
    [[nodiscard]] virtual bool Setup(const RendererServices& services) = 0;

    // Per-frame record. For MainColor features the command buffer is
    // already inside vkCmdBeginRendering on the swapchain image. Features
    // in future phases that open their own passes own their own begin/end.
    virtual void OnDraw(const FrameContext& frame) = 0;

    // Runs in ~Renderer before any Vulkan service is torn down. Release
    // any GPU resources the feature still holds here.
    virtual void Teardown() {}
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
    template <typename T>
    T* AddFeature(std::unique_ptr<T> feature)
    {
        static_assert(std::is_base_of_v<IRenderFeature, T>,
                      "T must derive from IRenderFeature");
        if (!Valid || !feature) return nullptr;
        return static_cast<T*>(
            AddFeatureImpl(std::unique_ptr<IRenderFeature>(feature.release())));
    }

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
    std::vector<VkImageLayout> ImageLayouts;
    std::unique_ptr<VulkanDepthTarget> DepthTarget;
    VkImageLayout DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    RendererFrameTiming LastTiming;
    FrameImageCapture ImageCapture;
    std::uint64_t FramesDrawn = 0;

    // Validates phase, runs Setup(), pushes into OwnedFeatures/PhaseBuckets.
    // Returns the raw pointer on success, nullptr on failure.
    IRenderFeature* AddFeatureImpl(std::unique_ptr<IRenderFeature> feature);

    void RecordOffscreenPhase(const VulkanFrame& frame);
    void RecordMainColorPhase(const VulkanFrame& frame);
};
