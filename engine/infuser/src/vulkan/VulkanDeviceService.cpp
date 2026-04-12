#include <vulkan/VulkanDeviceService.h>
#include <vulkan/VulkanInstanceService.h>
#include <vulkan/IVulkanSurfaceProvider.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <format>

VulkanDeviceService::VulkanDeviceService(Logger& logger,
                                         VulkanInstanceService& instance,
                                         const CreateInfo& info,
                                         const IVulkanSurfaceProvider* surfaceProvider)
    : Log(logger)
    , Instance(instance.GetInstance())
{
    if (!instance.IsValid())
    {
        Log.Error("Cannot create device: VulkanInstanceService is not valid");
        return;
    }

    if (surfaceProvider)
    {
        auto result = surfaceProvider->CreateSurface(Instance);
        if (!result.Succeeded())
        {
            Log.Error("Surface creation failed: {}", result.Error);
            return;
        }
        Surface = result.Surface;
        Log.Info("Surface created via provider");
    }

    if (!PickPhysicalDevice(info))
    {
        Log.Error("Failed to find a suitable physical device");
        return;
    }

    if (!CreateLogicalDevice(info))
    {
        Log.Error("Failed to create logical device");
        return;
    }

    RetrieveQueues();
}

VulkanDeviceService::~VulkanDeviceService()
{
    if (Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Device);
        vkDestroyDevice(Device, nullptr);
        Log.Info("Vulkan logical device destroyed");
    }

    if (Surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(Instance, Surface, nullptr);
        Log.Info("Vulkan surface destroyed");
    }
}

bool VulkanDeviceService::PickPhysicalDevice(const CreateInfo& info)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(Instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        Log.Error("No Vulkan-capable physical devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(Instance, &deviceCount, devices.data());

    Log.Info("Found {} physical device(s):", deviceCount);
    for (auto d : devices)
        LogDeviceProperties(d);

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (auto d : devices)
    {
        int score = RateDevice(d, info);
        if (score > bestScore)
        {
            bestScore = score;
            bestDevice = d;
        }
    }

    if (bestDevice == VK_NULL_HANDLE || bestScore < 0)
        return false;

    PhysicalDevice = bestDevice;
    QueueFamilies = FindQueueFamilies(PhysicalDevice);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(PhysicalDevice, &props);
    Log.Info("Selected device: {} (score {})", props.deviceName, bestScore);

    return true;
}

int VulkanDeviceService::RateDevice(VkPhysicalDevice device, const CreateInfo& info) const
{
    if (!CheckDeviceExtensionSupport(device, info))
        return -1;

    auto families = FindQueueFamilies(device);
    bool requirePresent = Surface != VK_NULL_HANDLE;
    if (!families.IsComplete(requirePresent))
        return -1;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(device, &features);

    int score = 0;

    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && info.PreferDiscreteGpu)
        score += 1000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 100;

    score += static_cast<int>(props.limits.maxImageDimension2D / 1024);

    if (families.HasCompute() && families.Graphics != families.Compute)
        score += 50;

    if (families.HasTransfer()
        && families.Graphics != families.Transfer
        && (!families.HasCompute() || families.Compute != families.Transfer))
        score += 50;

    return score;
}

bool VulkanDeviceService::CheckDeviceExtensionSupport(VkPhysicalDevice device,
                                                      const CreateInfo& info) const
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    for (const auto* required : info.RequiredDeviceExtensions)
    {
        bool found = false;
        for (const auto& ext : available)
        {
            if (std::strcmp(ext.extensionName, required) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    return true;
}

VulkanQueueFamilies VulkanDeviceService::FindQueueFamilies(VkPhysicalDevice device) const
{
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);

    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    VulkanQueueFamilies result;

    for (uint32_t i = 0; i < familyCount; ++i)
    {
        const auto& family = families[i];

        if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !result.HasGraphics())
            result.Graphics = i;

        if ((family.queueFlags & VK_QUEUE_COMPUTE_BIT) && !result.HasCompute())
        {
            if (!(family.queueFlags & VK_QUEUE_GRAPHICS_BIT) || !result.HasCompute())
                result.Compute = i;
        }

        if ((family.queueFlags & VK_QUEUE_TRANSFER_BIT)
            && !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            && !(family.queueFlags & VK_QUEUE_COMPUTE_BIT))
        {
            result.Transfer = i;
        }

        if (Surface != VK_NULL_HANDLE && !result.HasPresent())
        {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, Surface, &presentSupport);
            if (presentSupport)
                result.Present = i;
        }
    }

    if (!result.HasCompute() && result.HasGraphics())
        result.Compute = result.Graphics;

    if (!result.HasTransfer() && result.HasGraphics())
        result.Transfer = result.Graphics;

    return result;
}

bool VulkanDeviceService::CreateLogicalDevice(const CreateInfo& info)
{
    std::set<uint32_t> uniqueFamilies;
    if (QueueFamilies.HasGraphics()) uniqueFamilies.insert(*QueueFamilies.Graphics);
    if (QueueFamilies.HasPresent())  uniqueFamilies.insert(*QueueFamilies.Present);
    if (QueueFamilies.HasCompute())  uniqueFamilies.insert(*QueueFamilies.Compute);
    if (QueueFamilies.HasTransfer()) uniqueFamilies.insert(*QueueFamilies.Transfer);

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueFamilies.size());

    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount       = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos       = queueCreateInfos.data();
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(info.RequiredDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = info.RequiredDeviceExtensions.data();

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

void VulkanDeviceService::RetrieveQueues()
{
    if (QueueFamilies.HasGraphics())
        vkGetDeviceQueue(Device, *QueueFamilies.Graphics, 0, &GraphicsQueue);

    if (QueueFamilies.HasPresent())
        vkGetDeviceQueue(Device, *QueueFamilies.Present, 0, &PresentQueue);

    if (QueueFamilies.HasCompute())
        vkGetDeviceQueue(Device, *QueueFamilies.Compute, 0, &ComputeQueue);

    if (QueueFamilies.HasTransfer())
        vkGetDeviceQueue(Device, *QueueFamilies.Transfer, 0, &TransferQueue);

    Log.Info("Queues retrieved — Graphics:{} Present:{} Compute:{} Transfer:{}",
             QueueFamilies.HasGraphics() ? std::to_string(*QueueFamilies.Graphics) : "none",
             QueueFamilies.HasPresent()  ? std::to_string(*QueueFamilies.Present)  : "none",
             QueueFamilies.HasCompute()  ? std::to_string(*QueueFamilies.Compute)  : "none",
             QueueFamilies.HasTransfer() ? std::to_string(*QueueFamilies.Transfer) : "none");
}

void VulkanDeviceService::LogDeviceProperties(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    const char* typeStr = "Unknown";
    switch (props.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeStr = "Discrete GPU";   break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeStr = "Virtual GPU";    break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeStr = "CPU";            break;
        default: break;
    }

    Log.Info("  {} [{}] (API {}.{}.{})",
             props.deviceName,
             typeStr,
             VK_API_VERSION_MAJOR(props.apiVersion),
             VK_API_VERSION_MINOR(props.apiVersion),
             VK_API_VERSION_PATCH(props.apiVersion));
}
