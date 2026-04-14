#include <render/backend/vulkan/VulkanAllocatorService.h>

#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanInstanceService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>

VulkanAllocatorService::VulkanAllocatorService(
    LoggingProvider& logging,
    VulkanInstanceService& instance,
    VulkanPhysicalDeviceService& physicalDevice,
    VulkanDeviceService& device)
    : Log(logging.GetLogger<VulkanAllocatorService>())
{
    if (!instance.IsValid() || !physicalDevice.IsValid() || !device.IsValid())
    {
        Log.Error("Cannot create VMA allocator: upstream Vulkan services not valid");
        return;
    }

    VmaAllocatorCreateInfo info{};
    info.physicalDevice = physicalDevice.GetPhysicalDevice();
    info.device = device.GetDevice();
    info.instance = instance.GetInstance();
    info.vulkanApiVersion = VK_API_VERSION_1_3;

    const VkResult result = vmaCreateAllocator(&info, &Allocator);
    if (result != VK_SUCCESS)
    {
        Log.Error("vmaCreateAllocator failed with code {}", static_cast<int>(result));
        Allocator = VK_NULL_HANDLE;
        return;
    }

    Log.Info("VMA allocator created (Vulkan 1.3)");
}

VulkanAllocatorService::~VulkanAllocatorService()
{
    if (Allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(Allocator);
        Log.Info("VMA allocator destroyed");
    }
}
