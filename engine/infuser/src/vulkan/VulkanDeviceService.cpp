#include <vulkan/VulkanDeviceService.h>

#include <vulkan/VulkanPhysicalDeviceService.h>

#include <set>

VulkanDeviceService::VulkanDeviceService(
    LoggingProvider& logging,
    VulkanPhysicalDeviceService& physicalDevice,
    const VulkanBootstrapPolicy& policy)
    : Log(logging.GetLogger<VulkanDeviceService>())
    , PhysicalDevice(physicalDevice.GetPhysicalDevice())
    , EnabledDeviceExtensions(physicalDevice.GetEnabledDeviceExtensions())
{
    if (!physicalDevice.IsValid())
    {
        Log.Error("Cannot create logical device: VulkanPhysicalDeviceService is not valid");
        return;
    }

    if (!CreateLogicalDevice(physicalDevice, policy))
    {
        Log.Error("Failed to create logical device");
    }
}

VulkanDeviceService::~VulkanDeviceService()
{
    if (Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Device);
        vkDestroyDevice(Device, nullptr);
        Log.Info("Vulkan logical device destroyed");
    }
}

bool VulkanDeviceService::CreateLogicalDevice(
    VulkanPhysicalDeviceService& physicalDevice,
    const VulkanBootstrapPolicy& policy)
{
    const auto& queueFamilies = physicalDevice.GetQueueFamilies();

    std::set<uint32_t> uniqueFamilies;
    if (queueFamilies.HasGraphics()) uniqueFamilies.insert(*queueFamilies.Graphics);
    if (queueFamilies.HasPresent())  uniqueFamilies.insert(*queueFamilies.Present);
    if (queueFamilies.HasCompute())  uniqueFamilies.insert(*queueFamilies.Compute);
    if (queueFamilies.HasTransfer()) uniqueFamilies.insert(*queueFamilies.Transfer);

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueFamilies.size());

    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &policy.DeviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(EnabledDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = EnabledDeviceExtensions.data();

    VkResult result = vkCreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateDevice failed with code {}", static_cast<int>(result));
        Device = VK_NULL_HANDLE;
        return false;
    }

    Log.Info("Vulkan logical device created");
    return true;
}
