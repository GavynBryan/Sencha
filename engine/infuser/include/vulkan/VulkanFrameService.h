#pragma once

#include <logging/Logger.h>
#include <service/IService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanDeviceService;
class VulkanQueueService;
class VulkanSwapchainService;

enum class VulkanFrameStatus
{
    Ready,
    SwapchainOutOfDate,
    SurfaceUnavailable,
    DeviceLost,
    Error
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
};

class VulkanFrameService : public IService
{
public:
    VulkanFrameService(Logger& logger,
                       VulkanDeviceService& device,
                       VulkanQueueService& queues,
                       VulkanSwapchainService& swapchain,
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

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    const VulkanQueueService& Queues;
    VulkanSwapchainService& Swapchain;
    std::vector<FrameData> Frames;
    std::vector<VkFence> ImageInFlightFences;
    std::vector<VkSemaphore> ImageRenderFinishedSemaphores;
    uint32_t CurrentFrame = 0;
    bool Valid = false;

    bool CreateFrameData(uint32_t framesInFlight);
    bool CreateImageSyncObjects();
    void DestroyImageSyncObjects();
    void DestroyFrameData();
    void AdvanceFrame();
};
