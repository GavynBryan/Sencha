#pragma once

#include <core/service/IService.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <vulkan/vulkan.h>
#include <vector>

class VulkanPhysicalDeviceService;

//=============================================================================
// VulkanDeviceService
//
// Owns VkDevice. Physical-device selection, surfaces, and queue retrieval are
// separate bootstrap services so their lifetimes and decisions stay visible.
//=============================================================================
class VulkanDeviceService : public IService
{
public:
    VulkanDeviceService(LoggingProvider& logging,
                        VulkanPhysicalDeviceService& physicalDevice,
                        const VulkanBootstrapPolicy& policy);
    ~VulkanDeviceService() override;

    VulkanDeviceService(const VulkanDeviceService&) = delete;
    VulkanDeviceService& operator=(const VulkanDeviceService&) = delete;
    VulkanDeviceService(VulkanDeviceService&&) = delete;
    VulkanDeviceService& operator=(VulkanDeviceService&&) = delete;

    bool IsValid() const { return Device != VK_NULL_HANDLE; }

    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
    [[nodiscard]] VkDevice GetDevice() const { return Device; }
    [[nodiscard]] const std::vector<const char*>& GetEnabledDeviceExtensions() const
    {
        return EnabledDeviceExtensions;
    }

private:
    Logger& Log;

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    std::vector<const char*> EnabledDeviceExtensions;

    bool CreateLogicalDevice(VulkanPhysicalDeviceService& physicalDevice,
                             const VulkanBootstrapPolicy& policy);
};
