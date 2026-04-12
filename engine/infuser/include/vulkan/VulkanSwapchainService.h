#pragma once

#include <logging/Logger.h>
#include <service/IService.h>
#include <window/WindowTypes.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanDeviceService;
class VulkanPhysicalDeviceService;
class VulkanQueueService;
class VulkanSurfaceService;

class VulkanSwapchainService : public IService
{
public:
    VulkanSwapchainService(Logger& logger,
                           VulkanDeviceService& device,
                           VulkanPhysicalDeviceService& physicalDevice,
                           VulkanSurfaceService& surface,
                           VulkanQueueService& queues,
                           WindowExtent desiredExtent);
    ~VulkanSwapchainService() override;

    VulkanSwapchainService(const VulkanSwapchainService&) = delete;
    VulkanSwapchainService& operator=(const VulkanSwapchainService&) = delete;
    VulkanSwapchainService(VulkanSwapchainService&&) = delete;
    VulkanSwapchainService& operator=(VulkanSwapchainService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Swapchain != VK_NULL_HANDLE; }
    [[nodiscard]] VkSwapchainKHR GetSwapchain() const { return Swapchain; }
    [[nodiscard]] VkFormat GetFormat() const { return Format; }
    [[nodiscard]] VkColorSpaceKHR GetColorSpace() const { return ColorSpace; }
    [[nodiscard]] VkPresentModeKHR GetPresentMode() const { return PresentMode; }
    [[nodiscard]] VkExtent2D GetExtent() const { return Extent; }
    [[nodiscard]] uint32_t GetImageCount() const { return static_cast<uint32_t>(Images.size()); }
    [[nodiscard]] VkImage GetImage(uint32_t index) const;
    [[nodiscard]] VkImageView GetImageView(uint32_t index) const;

    bool Recreate(WindowExtent desiredExtent);

private:
    struct SwapchainSupport
    {
        VkSurfaceCapabilitiesKHR Capabilities{};
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
        bool Valid = false;
    };

    Logger& Log;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    const VulkanQueueService& Queues;

    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> Images;
    std::vector<VkImageView> ImageViews;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D Extent{};

    [[nodiscard]] SwapchainSupport QuerySupport() const;
    [[nodiscard]] bool HasUsableSupport(const SwapchainSupport& support) const;
    [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR ChoosePresentMode(
        const std::vector<VkPresentModeKHR>& presentModes) const;
    [[nodiscard]] VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(
        VkCompositeAlphaFlagsKHR supportedFlags) const;
    [[nodiscard]] VkExtent2D ChooseExtent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        WindowExtent desiredExtent) const;
    [[nodiscard]] uint32_t ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities) const;

    bool CreateSwapchain(WindowExtent desiredExtent);
    bool CreateImageViews();
    void DestroySwapchain();
};
