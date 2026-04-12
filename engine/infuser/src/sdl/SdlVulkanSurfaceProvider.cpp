#include <sdl/SdlVulkanSurfaceProvider.h>
#include <sdl/SdlWindow.h>

#include <SDL3/SDL_vulkan.h>

SdlVulkanSurfaceProvider::SdlVulkanSurfaceProvider(Logger& logger, SdlWindow& window)
    : Log(logger), Window(window)
{
}

std::vector<const char*> SdlVulkanSurfaceProvider::GetRequiredInstanceExtensions() const
{
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names || count == 0)
    {
        Log.Error("Failed to query SDL Vulkan extensions: {}", SDL_GetError());
        return {};
    }
    return { names, names + count };
}

VulkanSurfaceResult SdlVulkanSurfaceProvider::CreateSurface(VkInstance instance) const
{
    if (!Window.IsValid())
        return { VK_NULL_HANDLE, "Window is not valid" };

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(Window.GetHandle(), instance, nullptr, &surface))
        return { VK_NULL_HANDLE, SDL_GetError() };

    Log.Info("Vulkan surface created");
    return { surface, {} };
}
