#include <render/backend/vulkan/VulkanQueueService.h>

#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>

#include <string>

VulkanQueueService::VulkanQueueService(
    LoggingProvider& logging,
    VulkanDeviceService& device,
    VulkanPhysicalDeviceService& physicalDevice,
    const VulkanBootstrapPolicy& policy)
    : Log(logging.GetLogger<VulkanQueueService>())
    , QueueFamilies(physicalDevice.GetQueueFamilies())
{
    if (!device.IsValid())
    {
        Log.Error("Cannot retrieve Vulkan queues: VulkanDeviceService is not valid");
        return;
    }

    if (!physicalDevice.IsValid())
    {
        Log.Error("Cannot retrieve Vulkan queues: VulkanPhysicalDeviceService is not valid");
        return;
    }

    VkDevice handle = device.GetDevice();

    if (QueueFamilies.HasGraphics())
    {
        vkGetDeviceQueue(handle, *QueueFamilies.Graphics, 0, &GraphicsQueue);
    }

    if (QueueFamilies.HasPresent())
    {
        vkGetDeviceQueue(handle, *QueueFamilies.Present, 0, &PresentQueue);
    }

    if (QueueFamilies.HasCompute())
    {
        vkGetDeviceQueue(handle, *QueueFamilies.Compute, 0, &ComputeQueue);
    }

    if (QueueFamilies.HasTransfer())
    {
        vkGetDeviceQueue(handle, *QueueFamilies.Transfer, 0, &TransferQueue);
    }

    Valid = ValidateRequiredQueues(policy);

    Log.Info("Queues retrieved - Graphics:{} Present:{} Compute:{} Transfer:{}",
             QueueFamilies.HasGraphics() ? std::to_string(*QueueFamilies.Graphics) : "none",
             QueueFamilies.HasPresent()  ? std::to_string(*QueueFamilies.Present)  : "none",
             QueueFamilies.HasCompute()  ? std::to_string(*QueueFamilies.Compute)  : "none",
             QueueFamilies.HasTransfer() ? std::to_string(*QueueFamilies.Transfer) : "none");
}

bool VulkanQueueService::ValidateRequiredQueues(const VulkanBootstrapPolicy& policy) const
{
    if (policy.RequiredQueues.Graphics && GraphicsQueue == VK_NULL_HANDLE)
    {
        Log.Error("Required graphics queue is unavailable");
        return false;
    }

    if (policy.RequiredQueues.Present && PresentQueue == VK_NULL_HANDLE)
    {
        Log.Error("Required present queue is unavailable");
        return false;
    }

    if (policy.RequiredQueues.Compute && ComputeQueue == VK_NULL_HANDLE)
    {
        Log.Error("Required compute queue is unavailable");
        return false;
    }

    if (policy.RequiredQueues.Transfer && TransferQueue == VK_NULL_HANDLE)
    {
        Log.Error("Required transfer queue is unavailable");
        return false;
    }

    return true;
}
