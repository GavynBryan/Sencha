#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanDeletionQueueService;
class VulkanDeviceService;
class VulkanQueueService;
class VulkanSwapchainService;

enum class VulkanFrameStatus
{
    Ready,
    SwapchainOutOfDate,
    SurfaceSuboptimal,
    SurfaceUnavailable,
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

struct VulkanFrame
{
    uint32_t FrameIndex = 0;
    uint32_t ImageIndex = 0;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkImage SwapchainImage = VK_NULL_HANDLE;
    VkImageView SwapchainImageView = VK_NULL_HANDLE;
    VkFormat SwapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D SwapchainExtent{};
    uint64_t SwapchainGeneration = 0;
};

struct VulkanFrameTiming
{
    double AcquireSeconds = 0.0;
    double SubmitSeconds = 0.0;
    double PresentSeconds = 0.0;
    uint32_t ImageIndex = 0;
    uint64_t SwapchainGeneration = 0;
};

class VulkanFrameService : public IService
{
public:
    VulkanFrameService(LoggingProvider& logging,
                       VulkanDeviceService& device,
                       VulkanQueueService& queues,
                       VulkanSwapchainService& swapchain,
                       VulkanDeletionQueueService& deletionQueue,
                       uint32_t framesInFlight = 2);
    ~VulkanFrameService() override;

    VulkanFrameService(const VulkanFrameService&) = delete;
    VulkanFrameService& operator=(const VulkanFrameService&) = delete;
    VulkanFrameService(VulkanFrameService&&) = delete;
    VulkanFrameService& operator=(VulkanFrameService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }
    [[nodiscard]] uint32_t GetFramesInFlight() const
    {
        return static_cast<uint32_t>(Frames.size());
    }

    VulkanFrameStatus BeginFrame(VulkanFrame& frame);
    VulkanFrameStatus EndFrame(const VulkanFrame& frame);
    void ResetAfterSwapchainRecreate();
    [[nodiscard]] const VulkanFrameTiming& GetLastTiming() const { return LastTiming; }

private:
    struct FrameData
    {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        VkFence InFlightFence = VK_NULL_HANDLE;
        bool Submitted = false;
        bool AcquireSuboptimal = false;
    };

    struct SwapchainImageFrameState
    {
        uint64_t Generation = 0;
        VkFence InFlightFence = VK_NULL_HANDLE;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    const VulkanQueueService& Queues;
    VulkanSwapchainService& Swapchain;
    VulkanDeletionQueueService* DeletionQueue = nullptr;
    std::vector<FrameData> Frames;
    std::vector<SwapchainImageFrameState> ImageInFlightFences;
    std::vector<VkSemaphore> ImageRenderFinishedSemaphores;
    uint32_t CurrentFrame = 0;
    bool Valid = false;
    VulkanFrameTiming LastTiming;

    bool CreateFrameData(uint32_t framesInFlight);
    bool CreateImageSyncObjects();
    void DestroyImageSyncObjects();
    void DestroyFrameData();
    void AdvanceFrame();
};
