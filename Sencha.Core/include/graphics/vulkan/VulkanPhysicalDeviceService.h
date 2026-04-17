#pragma once

#include <core/service/IService.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <graphics/vulkan/VulkanQueueFamilies.h>
#include <vulkan/vulkan.h>

#include <vector>

class VulkanInstanceService;
class VulkanSurfaceService;

class VulkanPhysicalDeviceService : public IService
{
public:
    VulkanPhysicalDeviceService(LoggingProvider& logging,
                                VulkanInstanceService& instance,
                                const VulkanBootstrapPolicy& policy,
                                const VulkanSurfaceService* surface = nullptr);
    ~VulkanPhysicalDeviceService() override = default;

    VulkanPhysicalDeviceService(const VulkanPhysicalDeviceService&) = delete;
    VulkanPhysicalDeviceService& operator=(const VulkanPhysicalDeviceService&) = delete;
    VulkanPhysicalDeviceService(VulkanPhysicalDeviceService&&) = delete;
    VulkanPhysicalDeviceService& operator=(VulkanPhysicalDeviceService&&) = delete;

    [[nodiscard]] bool IsValid() const { return PhysicalDevice != VK_NULL_HANDLE; }
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
    [[nodiscard]] const VkPhysicalDeviceProperties& GetProperties() const { return Properties; }
    [[nodiscard]] const VkPhysicalDeviceFeatures& GetAvailableFeatures() const { return AvailableFeatures; }
    [[nodiscard]] const VulkanQueueFamilies& GetQueueFamilies() const { return QueueFamilies; }
    [[nodiscard]] const std::vector<const char*>& GetEnabledDeviceExtensions() const
    {
        return EnabledDeviceExtensions;
    }

private:
    Logger& Log;
    VkInstance Instance = VK_NULL_HANDLE;
    const VulkanSurfaceService* Surface = nullptr;

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties Properties{};
    VkPhysicalDeviceFeatures AvailableFeatures{};
    VulkanQueueFamilies QueueFamilies;
    std::vector<const char*> EnabledDeviceExtensions;

    bool PickPhysicalDevice(const VulkanBootstrapPolicy& policy);
    int RateDevice(VkPhysicalDevice device, const VulkanBootstrapPolicy& policy) const;
    bool CheckRequiredDeviceExtensions(VkPhysicalDevice device,
                                       const VulkanBootstrapPolicy& policy) const;
    bool CheckRequiredFeatures(VkPhysicalDevice device,
                               const VkPhysicalDeviceFeatures& required) const;
    bool SupportsDeviceExtension(VkPhysicalDevice device, const char* extension) const;
    VulkanQueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
    void BuildEnabledDeviceExtensions(const VulkanBootstrapPolicy& policy);
    void LogDeviceProperties(VkPhysicalDevice device) const;
};
