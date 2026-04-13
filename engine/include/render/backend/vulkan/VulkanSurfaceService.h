#pragma once

#include <core/service/IService.h>
#include <core/logging/LoggingProvider.h>
#include <vulkan/vulkan.h>

class SdlWindow;
class VulkanInstanceService;

class VulkanSurfaceService : public IService
{
public:
    VulkanSurfaceService(LoggingProvider& logging,
                         VulkanInstanceService& instance,
                         const SdlWindow& window);
    ~VulkanSurfaceService() override;

    VulkanSurfaceService(const VulkanSurfaceService&) = delete;
    VulkanSurfaceService& operator=(const VulkanSurfaceService&) = delete;
    VulkanSurfaceService(VulkanSurfaceService&&) = delete;
    VulkanSurfaceService& operator=(VulkanSurfaceService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Surface != VK_NULL_HANDLE; }
    [[nodiscard]] VkSurfaceKHR GetSurface() const { return Surface; }
    [[nodiscard]] bool SupportsPresent(VkPhysicalDevice device, uint32_t queueFamily) const;

private:
    Logger& Log;
    VkInstance Instance = VK_NULL_HANDLE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
};
