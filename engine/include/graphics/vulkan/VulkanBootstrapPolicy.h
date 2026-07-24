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
        // Cooked textures are BC-compressed (docs/assets/pipeline.md,
        // Decision L); BC is universal on desktop hardware, which is the
        // only target the Vulkan backend serves.
        DeviceFeatures.textureCompressionBC = VK_TRUE;
        // The lighting descriptor set binds a cube-array shadow map (dummy
        // until point shadows land). Universal on desktop hardware.
        DeviceFeatures.imageCubeArray = VK_TRUE;
    }

    std::string AppName = "Sencha";
    uint32_t AppVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    uint32_t ApiVersion = VK_API_VERSION_1_3;

    bool EnableValidation = true;
    // Extra validation checks, each requesting a VkValidationFeatureEnableEXT
    // on the layer. They have no effect unless EnableValidation is on.
    bool ValidateSynchronization = false;
    bool ValidateGpuAssisted = false;
    bool ValidateBestPractices = false;
    bool PreferDiscreteGpu = true;
    // Index into the enumeration order, overriding scoring. Comparing the same
    // scene across the adapters in one machine is otherwise not expressible:
    // scoring always lands on the same device. A negative value scores
    // normally; an out-of-range or unsuitable index fails selection rather
    // than silently falling back to a different adapter than the one asked
    // for, which would misattribute every measurement taken after it.
    std::int32_t DeviceIndex = -1;

    std::vector<const char*> RequiredInstanceExtensions;
    std::vector<const char*> OptionalInstanceExtensions;
    std::vector<const char*> RequiredDeviceExtensions;
    std::vector<const char*> OptionalDeviceExtensions;

    VulkanQueueRequirements RequiredQueues;
    VkPhysicalDeviceFeatures DeviceFeatures{};
};
