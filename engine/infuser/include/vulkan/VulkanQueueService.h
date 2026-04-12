#pragma once

#include <service/IService.h>
#include <logging/Logger.h>
#include <vulkan/VulkanBootstrapPolicy.h>
#include <vulkan/VulkanQueueFamilies.h>
#include <vulkan/vulkan.h>

class VulkanDeviceService;
class VulkanPhysicalDeviceService;

class VulkanQueueService : public IService
{
public:
    VulkanQueueService(Logger& logger,
                       VulkanDeviceService& device,
                       VulkanPhysicalDeviceService& physicalDevice,
                       const VulkanBootstrapPolicy& policy);
    ~VulkanQueueService() override = default;

    VulkanQueueService(const VulkanQueueService&) = delete;
    VulkanQueueService& operator=(const VulkanQueueService&) = delete;
    VulkanQueueService(VulkanQueueService&&) = delete;
    VulkanQueueService& operator=(VulkanQueueService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }
    [[nodiscard]] const VulkanQueueFamilies& GetQueueFamilies() const { return QueueFamilies; }

    [[nodiscard]] VkQueue GetGraphicsQueue() const { return GraphicsQueue; }
    [[nodiscard]] VkQueue GetPresentQueue() const { return PresentQueue; }
    [[nodiscard]] VkQueue GetComputeQueue() const { return ComputeQueue; }
    [[nodiscard]] VkQueue GetTransferQueue() const { return TransferQueue; }

private:
    bool ValidateRequiredQueues(const VulkanBootstrapPolicy& policy) const;

    Logger& Log;
    VulkanQueueFamilies QueueFamilies;

    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    VkQueue PresentQueue = VK_NULL_HANDLE;
    VkQueue ComputeQueue = VK_NULL_HANDLE;
    VkQueue TransferQueue = VK_NULL_HANDLE;
    bool Valid = false;
};
