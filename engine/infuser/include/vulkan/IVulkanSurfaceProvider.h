#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

struct VulkanSurfaceResult
{
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    std::string  Error;

    [[nodiscard]] bool Succeeded() const noexcept { return Surface != VK_NULL_HANDLE; }
};

class IVulkanSurfaceProvider
{
public:
    virtual ~IVulkanSurfaceProvider() = default;

    [[nodiscard]] virtual std::vector<const char*> GetRequiredInstanceExtensions() const = 0;
    [[nodiscard]] virtual VulkanSurfaceResult CreateSurface(VkInstance instance) const = 0;
};
