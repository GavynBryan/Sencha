#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

//=============================================================================
// IVulkanSurfaceProvider
//
// Narrow interface that decouples Vulkan from any specific windowing library.
// A concrete implementation (SDL3, GLFW, Win32, headless, etc.) supplies:
//   - The instance extensions it needs (e.g. VK_KHR_surface, VK_KHR_win32_surface)
//   - A factory method to create a VkSurfaceKHR from a live VkInstance
//
// This is NOT a service. It is passed into VulkanDeviceService at construction
// time as an optional dependency. If null, the device is created without
// present capabilities.
//=============================================================================
class IVulkanSurfaceProvider
{
public:
    virtual ~IVulkanSurfaceProvider() = default;

    virtual std::vector<const char*> GetRequiredInstanceExtensions() const = 0;

    virtual VkSurfaceKHR CreateSurface(VkInstance instance) const = 0;
};
