#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanFrameService.h>
#include <vulkan/vulkan.h>

#include <cassert>
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
// FeatureHandle<T>
//
// Lightweight typed handle to a registered render feature. Two uint32_ts,
// trivially copyable, no heap allocation. The Index addresses a slot in the
// Renderer's stable slot array; Generation is the value that slot held when
// the feature was registered. Any mismatch on resolve → stale handle.
//=============================================================================
template <typename T>
struct FeatureHandle
{
    uint32_t Index      = ~0u;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsNull() const { return Index == ~0u; }
};

// Forward declaration so Renderer::AddFeature can name FeatureRef<T> as its
// return type before the full class definition appears below.
template <typename T>
class FeatureRef;

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

    // Take ownership of a feature, run its Setup(), and return a typed
    // FeatureRef<T>. The ref remains valid for the lifetime of this Renderer
    // or until the feature is explicitly unregistered. Returns an empty ref
    // on failure (invalid renderer, null feature, or bad phase).
    //
    // Defined after FeatureRef<T> below so that class is complete.
    template <typename T>
    FeatureRef<T> AddFeature(std::unique_ptr<T> feature);

    // O(1) handle resolver: bounds check, null check, generation match, cast.
    // Returns nullptr if the handle is stale. Safe to call from any context.
    //
    // The static_cast is well-defined: slots are only written by AddFeature<T>
    // which ensures the IRenderFeature* is actually a T*.
    template <typename T>
    [[nodiscard]] T* ResolveFeature(FeatureHandle<T> handle) const
    {
        if (handle.Index >= static_cast<uint32_t>(FeatureSlots.size()))
            return nullptr;
        const FeatureSlot& slot = FeatureSlots[handle.Index];
        if (slot.Feature == nullptr || slot.Generation != handle.Generation)
            return nullptr;
        return static_cast<T*>(slot.Feature);
    }

    // One-call frame driver: acquire -> scratch rotate -> phase iterate ->
    // transition -> present. Returns SwapchainOutOfDate if the caller
    // needs to recreate the swapchain and then invoke NotifySwapchainRecreated.
    DrawStatus DrawFrame();

    // Reset per-swapchain-image tracking after VulkanSwapchainService::Recreate.
    void NotifySwapchainRecreated();

private:
    // Stable slot storage. Slots are appended or vacated, never removed or
    // reordered, so the Index embedded in a FeatureHandle stays valid for the
    // Renderer's entire lifetime. On vacate, Generation is incremented so any
    // handle carrying the old generation becomes stale on the next Resolve.
    struct FeatureSlot
    {
        IRenderFeature* Feature   = nullptr;
        uint32_t        Generation = 0;
    };

    Logger& Log;
    VulkanSwapchainService& Swapchain;
    VulkanFrameService& Frames;
    RendererServices Services;
    bool Valid = false;

    std::vector<FeatureSlot>               FeatureSlots;
    std::vector<std::unique_ptr<IRenderFeature>> OwnedFeatures;
    std::vector<IRenderFeature*>           PhaseBuckets[static_cast<size_t>(RenderPhase::Count)];
    std::vector<VkImageLayout>             ImageLayouts;

    // Returns the slot index on success, ~0u on failure.
    uint32_t AddFeatureImpl(std::unique_ptr<IRenderFeature> feature);

    // Vacates all slots and bumps their generations, atomically invalidating
    // every outstanding FeatureRef. Called at the top of ~Renderer before
    // any Teardown() runs.
    void UnregisterAllFeatures();

    void RecordMainColorPhase(const VulkanFrame& frame);
};

//=============================================================================
// FeatureRef<T>
//
// Non-owning resolver handle. Stores a Renderer* and a FeatureHandle<T>.
// Every dereference pays one bounds check + one generation compare -- no
// atomics, no heap, no shared ownership. IsValid() / Get() are safe to call
// after the Renderer is destroyed (Owner still points at dead memory, so
// don't keep FeatureRefs past that lifetime -- but handle.Index will fail the
// bounds/generation check immediately against the cleared slots).
//
// In the normal case (Renderer outlives FeatureRef), stale detection fires
// when a feature is explicitly unregistered or when ~Renderer calls
// UnregisterAllFeatures() before running any Teardown().
//=============================================================================
template <typename T>
class FeatureRef
{
public:
    FeatureRef() = default;

    FeatureRef(Renderer* owner, FeatureHandle<T> handle)
        : Owner(owner), Handle(handle) {}

    // Resolves the handle. Returns nullptr if stale. O(1).
    [[nodiscard]] T* Get() const
    {
        return Owner ? Owner->ResolveFeature(Handle) : nullptr;
    }

    [[nodiscard]] bool IsValid() const { return Get() != nullptr; }
    explicit operator bool() const { return IsValid(); }

    T* operator->() const
    {
        T* ptr = Get();
        assert(ptr != nullptr && "FeatureRef: accessed stale handle (feature unregistered or Renderer destroyed)");
        return ptr;
    }

    T& operator*() const
    {
        T* ptr = Get();
        assert(ptr != nullptr && "FeatureRef: accessed stale handle (feature unregistered or Renderer destroyed)");
        return *ptr;
    }

private:
    Renderer*        Owner  = nullptr;
    FeatureHandle<T> Handle = {};
};

//=============================================================================
// Renderer::AddFeature -- defined here so FeatureRef<T> is complete.
//=============================================================================
template <typename T>
inline FeatureRef<T> Renderer::AddFeature(std::unique_ptr<T> feature)
{
    static_assert(std::is_base_of_v<IRenderFeature, T>,
                  "T must derive from IRenderFeature");
    if (!Valid || !feature) return {};

    const uint32_t index = AddFeatureImpl(
        std::unique_ptr<IRenderFeature>(feature.release()));

    if (index == ~0u) return {};

    return FeatureRef<T>(this, FeatureHandle<T>{index, FeatureSlots[index].Generation});
}
