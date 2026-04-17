#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct VulkanQueueRequirements
{
    bool Graphics = true;
    bool Present = false;
    bool Compute = false;
    bool Transfer = false;
};

struct VulkanBootstrapPolicy
{
    VulkanBootstrapPolicy()
    {
        // Sencha always requires anisotropic sampling so VulkanSamplerCache
        // callers can opt into it without a feature-enable dance.
        DeviceFeatures.samplerAnisotropy = VK_TRUE;
    }

    std::string AppName = "Sencha";
    uint32_t AppVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    uint32_t ApiVersion = VK_API_VERSION_1_3;

    bool EnableValidation = true;
    bool PreferDiscreteGpu = true;

    std::vector<const char*> RequiredInstanceExtensions;
    std::vector<const char*> OptionalInstanceExtensions;
    std::vector<const char*> RequiredDeviceExtensions;
    std::vector<const char*> OptionalDeviceExtensions;

    VulkanQueueRequirements RequiredQueues;
    VkPhysicalDeviceFeatures DeviceFeatures{};
};
