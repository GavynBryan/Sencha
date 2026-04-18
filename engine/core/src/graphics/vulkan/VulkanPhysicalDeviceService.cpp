#include <graphics/vulkan/VulkanPhysicalDeviceService.h>

#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanSurfaceService.h>

#include <algorithm>
#include <cstring>

namespace
{
    bool FeatureRequested(VkBool32 value)
    {
        return value == VK_TRUE;
    }

    bool FeatureMissing(VkBool32 required, VkBool32 available)
    {
        return FeatureRequested(required) && available != VK_TRUE;
    }

    bool ContainsExtension(const std::vector<const char*>& extensions, const char* extension)
    {
        return std::any_of(extensions.begin(), extensions.end(),
            [extension](const char* existing)
            {
                return std::strcmp(existing, extension) == 0;
            });
    }

    void AddUniqueExtension(std::vector<const char*>& extensions, const char* extension)
    {
        if (!ContainsExtension(extensions, extension))
        {
            extensions.push_back(extension);
        }
    }
}

VulkanPhysicalDeviceService::VulkanPhysicalDeviceService(
    LoggingProvider& logging,
    VulkanInstanceService& instance,
    const VulkanBootstrapPolicy& policy,
    const VulkanSurfaceService* surface)
    : Log(logging.GetLogger<VulkanPhysicalDeviceService>())
    , Instance(instance.GetInstance())
    , Surface(surface)
{
    if (!instance.IsValid())
    {
        Log.Error("Cannot select physical device: VulkanInstanceService is not valid");
        return;
    }

    if (policy.RequiredQueues.Present && (!Surface || !Surface->IsValid()))
    {
        Log.Error("Cannot require a present queue without a valid VulkanSurfaceService");
        return;
    }

    if (!PickPhysicalDevice(policy))
    {
        Log.Error("Failed to find a suitable physical device");
        return;
    }

    BuildEnabledDeviceExtensions(policy);
}

bool VulkanPhysicalDeviceService::PickPhysicalDevice(const VulkanBootstrapPolicy& policy)
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
    for (auto device : devices)
    {
        LogDeviceProperties(device);
    }

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (auto device : devices)
    {
        int score = RateDevice(device, policy);
        if (score > bestScore)
        {
            bestScore = score;
            bestDevice = device;
        }
    }

    if (bestDevice == VK_NULL_HANDLE || bestScore < 0)
    {
        return false;
    }

    PhysicalDevice = bestDevice;
    QueueFamilies = FindQueueFamilies(PhysicalDevice);
    vkGetPhysicalDeviceProperties(PhysicalDevice, &Properties);
    vkGetPhysicalDeviceFeatures(PhysicalDevice, &AvailableFeatures);

    Log.Info("Selected device: {} (score {})", Properties.deviceName, bestScore);
    return true;
}

int VulkanPhysicalDeviceService::RateDevice(
    VkPhysicalDevice device,
    const VulkanBootstrapPolicy& policy) const
{
    if (!CheckRequiredDeviceExtensions(device, policy))
    {
        return -1;
    }

    if (policy.RequiredQueues.Present
        && !SupportsDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        return -1;
    }

    if (!CheckRequiredFeatures(device, policy.DeviceFeatures))
    {
        return -1;
    }

    // Sencha floor: Vulkan 1.3 core with synchronization2 + dynamicRendering,
    // plus the Vulkan 1.2 descriptor-indexing feature set that the bindless
    // descriptor cache relies on. Every renderer service downstream assumes
    // all of these are on.
    {
        VkPhysicalDeviceVulkan12Features v12{};
        v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceVulkan13Features v13{};
        v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        v13.pNext = &v12;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &v13;

        vkGetPhysicalDeviceFeatures2(device, &features2);

        if (!v13.synchronization2 || !v13.dynamicRendering)
        {
            return -1;
        }

        if (!v12.descriptorIndexing
            || !v12.runtimeDescriptorArray
            || !v12.descriptorBindingPartiallyBound
            || !v12.descriptorBindingVariableDescriptorCount
            || !v12.descriptorBindingSampledImageUpdateAfterBind
            || !v12.shaderSampledImageArrayNonUniformIndexing)
        {
            return -1;
        }
    }

    auto families = FindQueueFamilies(device);
    if (!families.Satisfies(policy.RequiredQueues))
    {
        return -1;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;

    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && policy.PreferDiscreteGpu)
    {
        score += 1000;
    }
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
    {
        score += 100;
    }

    score += static_cast<int>(props.limits.maxImageDimension2D / 1024);

    if (families.HasCompute() && families.Graphics != families.Compute)
    {
        score += 50;
    }

    if (families.HasTransfer()
        && families.Graphics != families.Transfer
        && (!families.HasCompute() || families.Compute != families.Transfer))
    {
        score += 50;
    }

    return score;
}

bool VulkanPhysicalDeviceService::CheckRequiredDeviceExtensions(
    VkPhysicalDevice device,
    const VulkanBootstrapPolicy& policy) const
{
    for (const auto* required : policy.RequiredDeviceExtensions)
    {
        if (!SupportsDeviceExtension(device, required))
        {
            return false;
        }
    }

    return true;
}

bool VulkanPhysicalDeviceService::CheckRequiredFeatures(
    VkPhysicalDevice device,
    const VkPhysicalDeviceFeatures& required) const
{
    VkPhysicalDeviceFeatures available;
    vkGetPhysicalDeviceFeatures(device, &available);

#define SENCHA_REQUIRE_FEATURE(name) if (FeatureMissing(required.name, available.name)) return false
    SENCHA_REQUIRE_FEATURE(robustBufferAccess);
    SENCHA_REQUIRE_FEATURE(fullDrawIndexUint32);
    SENCHA_REQUIRE_FEATURE(imageCubeArray);
    SENCHA_REQUIRE_FEATURE(independentBlend);
    SENCHA_REQUIRE_FEATURE(geometryShader);
    SENCHA_REQUIRE_FEATURE(tessellationShader);
    SENCHA_REQUIRE_FEATURE(sampleRateShading);
    SENCHA_REQUIRE_FEATURE(dualSrcBlend);
    SENCHA_REQUIRE_FEATURE(logicOp);
    SENCHA_REQUIRE_FEATURE(multiDrawIndirect);
    SENCHA_REQUIRE_FEATURE(drawIndirectFirstInstance);
    SENCHA_REQUIRE_FEATURE(depthClamp);
    SENCHA_REQUIRE_FEATURE(depthBiasClamp);
    SENCHA_REQUIRE_FEATURE(fillModeNonSolid);
    SENCHA_REQUIRE_FEATURE(depthBounds);
    SENCHA_REQUIRE_FEATURE(wideLines);
    SENCHA_REQUIRE_FEATURE(largePoints);
    SENCHA_REQUIRE_FEATURE(alphaToOne);
    SENCHA_REQUIRE_FEATURE(multiViewport);
    SENCHA_REQUIRE_FEATURE(samplerAnisotropy);
    SENCHA_REQUIRE_FEATURE(textureCompressionETC2);
    SENCHA_REQUIRE_FEATURE(textureCompressionASTC_LDR);
    SENCHA_REQUIRE_FEATURE(textureCompressionBC);
    SENCHA_REQUIRE_FEATURE(occlusionQueryPrecise);
    SENCHA_REQUIRE_FEATURE(pipelineStatisticsQuery);
    SENCHA_REQUIRE_FEATURE(vertexPipelineStoresAndAtomics);
    SENCHA_REQUIRE_FEATURE(fragmentStoresAndAtomics);
    SENCHA_REQUIRE_FEATURE(shaderTessellationAndGeometryPointSize);
    SENCHA_REQUIRE_FEATURE(shaderImageGatherExtended);
    SENCHA_REQUIRE_FEATURE(shaderStorageImageExtendedFormats);
    SENCHA_REQUIRE_FEATURE(shaderStorageImageMultisample);
    SENCHA_REQUIRE_FEATURE(shaderStorageImageReadWithoutFormat);
    SENCHA_REQUIRE_FEATURE(shaderStorageImageWriteWithoutFormat);
    SENCHA_REQUIRE_FEATURE(shaderUniformBufferArrayDynamicIndexing);
    SENCHA_REQUIRE_FEATURE(shaderSampledImageArrayDynamicIndexing);
    SENCHA_REQUIRE_FEATURE(shaderStorageBufferArrayDynamicIndexing);
    SENCHA_REQUIRE_FEATURE(shaderStorageImageArrayDynamicIndexing);
    SENCHA_REQUIRE_FEATURE(shaderClipDistance);
    SENCHA_REQUIRE_FEATURE(shaderCullDistance);
    SENCHA_REQUIRE_FEATURE(shaderFloat64);
    SENCHA_REQUIRE_FEATURE(shaderInt64);
    SENCHA_REQUIRE_FEATURE(shaderInt16);
    SENCHA_REQUIRE_FEATURE(shaderResourceResidency);
    SENCHA_REQUIRE_FEATURE(shaderResourceMinLod);
    SENCHA_REQUIRE_FEATURE(sparseBinding);
    SENCHA_REQUIRE_FEATURE(sparseResidencyBuffer);
    SENCHA_REQUIRE_FEATURE(sparseResidencyImage2D);
    SENCHA_REQUIRE_FEATURE(sparseResidencyImage3D);
    SENCHA_REQUIRE_FEATURE(sparseResidency2Samples);
    SENCHA_REQUIRE_FEATURE(sparseResidency4Samples);
    SENCHA_REQUIRE_FEATURE(sparseResidency8Samples);
    SENCHA_REQUIRE_FEATURE(sparseResidency16Samples);
    SENCHA_REQUIRE_FEATURE(sparseResidencyAliased);
    SENCHA_REQUIRE_FEATURE(variableMultisampleRate);
    SENCHA_REQUIRE_FEATURE(inheritedQueries);
#undef SENCHA_REQUIRE_FEATURE

    return true;
}

bool VulkanPhysicalDeviceService::SupportsDeviceExtension(
    VkPhysicalDevice device,
    const char* extension) const
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    for (const auto& availableExtension : available)
    {
        if (std::strcmp(availableExtension.extensionName, extension) == 0)
        {
            return true;
        }
    }

    return false;
}

VulkanQueueFamilies VulkanPhysicalDeviceService::FindQueueFamilies(VkPhysicalDevice device) const
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
        {
            result.Graphics = i;
        }

        if (family.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            if (!result.HasCompute() || !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                result.Compute = i;
            }
        }

        if (family.queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            const bool dedicatedTransfer =
                !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                && !(family.queueFlags & VK_QUEUE_COMPUTE_BIT);
            if (!result.HasTransfer() || dedicatedTransfer)
            {
                result.Transfer = i;
            }
        }

        if (Surface && Surface->IsValid() && !result.HasPresent())
        {
            if (Surface->SupportsPresent(device, i))
            {
                result.Present = i;
            }
        }
    }

    if (!result.HasCompute() && result.HasGraphics())
    {
        result.Compute = result.Graphics;
    }

    if (!result.HasTransfer() && result.HasGraphics())
    {
        result.Transfer = result.Graphics;
    }

    return result;
}

void VulkanPhysicalDeviceService::BuildEnabledDeviceExtensions(
    const VulkanBootstrapPolicy& policy)
{
    if (policy.RequiredQueues.Present)
    {
        AddUniqueExtension(EnabledDeviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    for (const auto* extension : policy.RequiredDeviceExtensions)
    {
        AddUniqueExtension(EnabledDeviceExtensions, extension);
    }

    for (const auto* extension : policy.OptionalDeviceExtensions)
    {
        if (SupportsDeviceExtension(PhysicalDevice, extension))
        {
            AddUniqueExtension(EnabledDeviceExtensions, extension);
        }
        else
        {
            Log.Info("Optional device extension unavailable: {}", extension);
        }
    }
}

void VulkanPhysicalDeviceService::LogDeviceProperties(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    const char* typeStr = "Unknown";
    switch (props.deviceType)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        typeStr = "Discrete GPU";
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        typeStr = "Integrated GPU";
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        typeStr = "Virtual GPU";
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        typeStr = "CPU";
        break;
    default:
        break;
    }

    Log.Info("  {} [{}] (API {}.{}.{})",
             props.deviceName,
             typeStr,
             VK_API_VERSION_MAJOR(props.apiVersion),
             VK_API_VERSION_MINOR(props.apiVersion),
             VK_API_VERSION_PATCH(props.apiVersion));
}
