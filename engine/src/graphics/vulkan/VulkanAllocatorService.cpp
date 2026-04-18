#include <graphics/vulkan/VulkanAllocatorService.h>

#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>

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
