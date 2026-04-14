#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanFrameService.h>
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
class VulkanFrameScratch;
class VulkanUploadContextService;

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
//     FrameContext and nothing else -- there are no ServiceHost lookups in
//     the hot loop. Features are expected to cache direct pointers during
//     Setup() from the RendererServices bundle.
//
//   * Phase-aware from day one. Even with only MainColor implemented today,
//     every feature is bucketed by the phase it reports, so adding offscreen
//     / shadow / UI phases later doesn't churn the feature interface.
//
//   * Contribute() is game-driven, not renderer-driven. The game constructs
//     features, calls Contribute() on each to fold extensions and feature
//     bits into its VulkanBootstrapPolicy, then brings up the Vulkan stack,
//     then hands the features to the Renderer via AddFeature(). This
//     resolves the chicken-and-egg where features need the device to Setup
//     but the device needs feature contributions to create.
//=============================================================================

enum class RenderPhase : uint8_t
{
    MainColor = 0,
    // Reserved for: Offscreen, Shadow, Opaque, Transparent, UI, Post...
    Count
};

// Direct service pointers handed to features in Setup(). Features should
// cache whichever ones they need and never reach for the ServiceHost again.
struct RendererServices
{
    LoggingProvider* Logging = nullptr;
    VulkanDeviceService* Device = nullptr;
    VulkanPhysicalDeviceService* PhysicalDevice = nullptr;
    VulkanQueueService* Queues = nullptr;
    VulkanAllocatorService* Allocator = nullptr;
    VulkanBufferService* Buffers = nullptr;
    VulkanImageService* Images = nullptr;
    VulkanSamplerCache* Samplers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VulkanUploadContextService* Upload = nullptr;
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
    RenderPhase Phase = RenderPhase::MainColor;
};

class IRenderFeature
{
public:
    virtual ~IRenderFeature() = default;

    // Which phase this feature runs in. One feature, one phase.
    [[nodiscard]] virtual RenderPhase GetPhase() const = 0;

    // Pre-device hook. The game calls this before creating the physical
    // device so the feature can fold device extensions / feature bits into
    // the bootstrap policy. The Renderer itself never invokes this -- by
    // the time the Renderer exists, the device already exists.
    virtual void Contribute(VulkanBootstrapPolicy& /*policy*/) {}

    // Runs once, inside Renderer::AddFeature. Cache service pointers here.
    // Do any up-front GPU resource creation here too.
    virtual void Setup(const RendererServices& services) = 0;

    // Per-frame record. For MainColor features the command buffer is
    // already inside vkCmdBeginRendering on the swapchain image. Features
    // in future phases that open their own passes own their own begin/end.
    virtual void OnDraw(const FrameContext& frame) = 0;

    // Runs in ~Renderer before any Vulkan service is torn down. Release
    // any GPU resources the feature still holds here.
    virtual void Teardown() {}
};

class Renderer : public IService
{
public:
    enum class DrawStatus
    {
        Ok,
        SwapchainOutOfDate, // caller should recreate the swapchain
        Skipped,            // frame wasn't renderable (e.g. minimized)
        Error
    };

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
             VulkanFrameScratch& scratch,
             VulkanUploadContextService& upload);
    ~Renderer() override;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // Take ownership of a feature and run its Setup() immediately. The
    // feature is inserted into its phase bucket in insertion order. Safe
    // to call any time before DrawFrame, typically at game boot.
    //
    // Returns the raw pointer so game code can keep a handle for things
    // like calling Submit() on a sprite feature. Lifetime is tied to the
    // Renderer itself -- safe to dereference until ~Renderer runs.
    template <typename T>
    T* AddFeature(std::unique_ptr<T> feature)
    {
        static_assert(std::is_base_of_v<IRenderFeature, T>,
                      "T must derive from IRenderFeature");
        T* raw = feature.get();
        AddFeatureImpl(std::unique_ptr<IRenderFeature>(feature.release()));
        return raw;
    }

    // One-call frame driver: acquire -> scratch rotate -> phase iterate ->
    // transition -> present. Returns SwapchainOutOfDate if the caller
    // needs to recreate the swapchain and then invoke NotifySwapchainRecreated.
    DrawStatus DrawFrame();

    // Reset per-swapchain-image tracking after VulkanSwapchainService::Recreate.
    void NotifySwapchainRecreated();

private:
    Logger& Log;
    VulkanSwapchainService& Swapchain;
    VulkanFrameService& Frames;
    RendererServices Services;
    bool Valid = false;

    std::vector<std::unique_ptr<IRenderFeature>> OwnedFeatures;
    std::vector<IRenderFeature*> PhaseBuckets[static_cast<size_t>(RenderPhase::Count)];
    std::vector<VkImageLayout> ImageLayouts;

    void AddFeatureImpl(std::unique_ptr<IRenderFeature> feature);
    void RecordMainColorPhase(const VulkanFrame& frame);
};
