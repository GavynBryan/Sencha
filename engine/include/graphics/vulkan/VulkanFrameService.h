#pragma once

#include <core/handle/HandlePool.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/GpuFrameRetirement.h>
#include <graphics/PresentationId.h>
#include <platform/WindowTypes.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

class PresentationTarget;
class SdlWindow;
class VulkanDeletionQueueService;
class VulkanDeviceService;
class VulkanImageService;
class VulkanInstanceService;
class VulkanPhysicalDeviceService;
class VulkanQueueService;
class VulkanSurfaceService;
class VulkanSwapchainService;
struct PresentationDesc;
enum class SurfaceOutcome : std::uint8_t;

enum class VulkanFrameStatus
{
    Ready,
    // No presentation could be acquired; nothing was recorded.
    NothingAcquired,
    DeviceLost,
    Error
};

enum class RenderFrameResult
{
    Presented,
    SwapchainOutOfDate,
    SurfaceSuboptimal,
    SkippedMinimized,
    Failed,
};

// One presentation this frame records into.
struct AcquiredPresentation
{
    PresentationId Id;
    PresentationTarget* Target = nullptr;
    uint32_t ImageIndex = 0;
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent2D Extent{};
    bool Suboptimal = false;
};

struct VulkanFrame
{
    uint32_t FrameIndex = 0;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    std::span<const AcquiredPresentation> Presentations;
    // The primary's acquire; Ok when it did not take part.
    SurfaceOutcome PrimaryAcquire{};
};

struct VulkanFrameTiming
{
    double AcquireSeconds = 0.0;
    double SubmitSeconds = 0.0;
    double PresentSeconds = 0.0;
    double PresentWaitSeconds = 0.0;
    uint32_t ImageIndex = 0;
    uint64_t SwapchainGeneration = 0;
};

// The frame slots -- one command buffer and fence per frame in flight -- and
// the presentations a frame acquires and presents. Presentations are peers;
// the primary is the one created with the service, the default target for
// feature registration and the one frame pacing follows.
class VulkanFrameService
{
public:
    struct Services
    {
        LoggingProvider* Logging = nullptr;
        VulkanInstanceService* Instance = nullptr;
        VulkanPhysicalDeviceService* PhysicalDevice = nullptr;
        VulkanDeviceService* Device = nullptr;
        VulkanQueueService* Queues = nullptr;
        VulkanImageService* Images = nullptr;
        VulkanDeletionQueueService* DeletionQueue = nullptr;
    };

    // `primarySurface` is the one the device was chosen against.
    VulkanFrameService(const Services& services, std::unique_ptr<VulkanSurfaceService> primarySurface,
                       SdlWindow& primaryWindow, uint32_t framesInFlight = 2);
    ~VulkanFrameService();

    VulkanFrameService(const VulkanFrameService&) = delete;
    VulkanFrameService& operator=(const VulkanFrameService&) = delete;
    VulkanFrameService(VulkanFrameService&&) = delete;
    VulkanFrameService& operator=(VulkanFrameService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }
    [[nodiscard]] uint32_t GetFramesInFlight() const
    {
        return static_cast<uint32_t>(Frames.size());
    }

    [[nodiscard]] PresentationId PrimaryPresentation() const { return Primary; }
    [[nodiscard]] VulkanSwapchainService& PrimarySwapchain() const;
    [[nodiscard]] PresentationTarget* FindPresentation(PresentationId id);
    [[nodiscard]] std::size_t PresentationCount() const { return Presentations.Size(); }
    template <typename Fn>
    void ForEachPresentation(Fn&& fn) { Presentations.ForEach(std::forward<Fn>(fn)); }

    // Null on failure: the window has no surface, or the device's present
    // queue cannot present to it.
    PresentationId CreatePresentation(SdlWindow& window, const PresentationDesc& desc);
    // Takes the presentation out of every later frame at once and destroys it
    // once the frames that could still use it have retired, then runs
    // `afterRetired`. Refused for the primary and for an unknown id.
    bool RetirePresentation(PresentationId id, std::function<void()> afterRetired);
    // Recreates `id`'s swapchain at `extent`; the caller has decided it is due.
    bool RebuildPresentation(PresentationId id, WindowExtent extent);
    // Rebuilds every presentation but the primary whose swapchain went out of
    // date or whose window changed size. The primary's rebuild belongs to the
    // frame loop, which lets a resize settle first.
    void RebuildStaleSecondaries();
    // Whether a presentation besides the primary could be shown this frame.
    [[nodiscard]] bool AnySecondaryVisible();

    VulkanFrameStatus BeginFrame(VulkanFrame& frame);
    // DeviceLost or Failed when the frame broke; otherwise the primary's
    // outcome, acquire and present together. Another presentation that went
    // out of date is marked for rebuild and does not colour the result.
    SurfaceOutcome EndFrame(const VulkanFrame& frame);
    [[nodiscard]] const VulkanFrameTiming& GetLastTiming() const { return LastTiming; }

    // The fence-anchored frame clock. Callers holding a GPU resource the
    // renderer may still be reading stamp it on release and free it once this
    // reports it retired, instead of counting frames themselves.
    [[nodiscard]] GpuFrameRetirement GetRetirement() const { return Retirement; }

private:
    struct FrameData
    {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkFence InFlightFence = VK_NULL_HANDLE;
        // Frame number that last submitted work on this slot. Waiting this
        // slot's fence proves that frame complete; recording the number rather
        // than deriving it from a count keeps the retirement boundary right
        // when a frame errors out before submitting.
        uint64_t SubmittedFrameNumber = 0;
        bool Submitted = false;
    };

    struct Retiree
    {
        std::unique_ptr<PresentationTarget> Target;
        uint64_t Stamp = 0;
        std::function<void()> AfterRetired;
    };

    Logger& Log;
    Services Deps;
    VkDevice Device = VK_NULL_HANDLE;
    std::vector<FrameData> Frames;
    HandlePool<PresentationIdTag, PresentationTarget> Presentations;
    PresentationId Primary;
    std::vector<Retiree> Retiring;
    std::vector<AcquiredPresentation> Acquired;
    // Reused per frame so a submit allocates nothing.
    std::vector<VkSemaphore> WaitSemaphores;
    std::vector<VkPipelineStageFlags> WaitStages;
    std::vector<VkSemaphore> SignalSemaphores;
    std::vector<VkSwapchainKHR> PresentSwapchains;
    std::vector<uint32_t> PresentImages;
    std::vector<VkResult> PresentResults;
    std::vector<uint64_t> PresentIds;
    uint32_t CurrentFrame = 0;
    uint64_t NextPresentId = 1;
    GpuFrameRetirement Retirement;
    PFN_vkWaitForPresentKHR WaitForPresentFn = nullptr;
    bool PresentWaitEnabled = false;
    bool Valid = false;
    VulkanFrameTiming LastTiming;

    [[nodiscard]] std::unique_ptr<PresentationTarget> MakeTarget(std::unique_ptr<VulkanSurfaceService> surface,
                                                                 SdlWindow& window, const PresentationDesc& desc);
    bool CreateFrameData(uint32_t framesInFlight);
    void DestroyFrameData();
    void DestroyRetiredPresentations();
    void WaitForPrimaryPresent();
    VulkanFrameStatus AcquirePresentations(VulkanFrame& frame);
    void AdvanceFrame();
};
