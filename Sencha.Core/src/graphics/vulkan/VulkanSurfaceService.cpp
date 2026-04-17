#include <graphics/vulkan/VulkanSurfaceService.h>

#include <window/SdlWindow.h>
#include <graphics/vulkan/VulkanInstanceService.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

VulkanSurfaceService::VulkanSurfaceService(LoggingProvider& logging,
                                           VulkanInstanceService& instance,
                                           const SdlWindow& window)
    : Log(logging.GetLogger<VulkanSurfaceService>())
    , Instance(instance.GetInstance())
{
    if (!instance.IsValid())
    {
        Log.Error("Cannot create Vulkan surface: instance is not valid");
        return;
    }

    if (!window.IsValid())
    {
        Log.Error("Cannot create Vulkan surface: window is not valid");
        return;
    }

    if (!SDL_Vulkan_CreateSurface(window.GetHandle(), Instance, nullptr, &Surface))
    {
        Log.Error("Failed to create Vulkan surface: {}", SDL_GetError());
        Surface = VK_NULL_HANDLE;
        return;
    }

    Log.Info("Vulkan surface created");
}

VulkanSurfaceService::~VulkanSurfaceService()
{
    if (Surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(Instance, Surface, nullptr);
        Log.Info("Vulkan surface destroyed");
    }
}

bool VulkanSurfaceService::SupportsPresent(VkPhysicalDevice device, uint32_t queueFamily) const
{
    if (Surface == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
    {
        return false;
    }

    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, queueFamily, Surface, &presentSupport);
    return presentSupport == VK_TRUE;
}
