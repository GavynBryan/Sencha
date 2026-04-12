#include <logging/ConsoleLogSink.h>
#include <sdl/SdlVideoService.h>
#include <sdl/SdlWindow.h>
#include <sdl/SdlWindowService.h>
#include <service/ServiceHost.h>
#include <vulkan/VulkanBootstrapPolicy.h>
#include <vulkan/VulkanDeviceService.h>
#include <vulkan/VulkanInstanceService.h>
#include <vulkan/VulkanPhysicalDeviceService.h>
#include <vulkan/VulkanQueueService.h>
#include <vulkan/VulkanSurfaceService.h>
#include <window/WindowCreateInfo.h>

#include <SDL3/SDL.h>

class SdlVulkanWindowExample
{
};

int main()
{
    ServiceHost services;
    auto& logging = services.GetLoggingProvider();
    logging.AddSink<ConsoleLogSink>();

    auto& logger = logging.GetLogger<SdlVulkanWindowExample>();

    WindowCreateInfo windowInfo;
    windowInfo.Title = "Sencha SDL3 Vulkan Window";
    windowInfo.Width = 1280;
    windowInfo.Height = 720;
    windowInfo.GraphicsApi = WindowGraphicsApi::Vulkan;
    windowInfo.Resizable = true;
    windowInfo.Visible = true;

    SdlVideoService video(logger);
    if (!video.IsValid())
    {
        return 1;
    }

    SdlWindowService windows(logger, video);
    SdlWindow* window = windows.CreateWindow(windowInfo);
    if (!window)
    {
        return 1;
    }

    auto windowId = window->GetId();

    VulkanBootstrapPolicy vulkanPolicy;
    vulkanPolicy.AppName = "SdlVulkanWindow";
    vulkanPolicy.RequiredQueues.Present = true;

    auto platformExtensions = windows.GetRequiredVulkanInstanceExtensions();
    vulkanPolicy.RequiredInstanceExtensions.insert(
        vulkanPolicy.RequiredInstanceExtensions.end(),
        platformExtensions.begin(),
        platformExtensions.end());

    VulkanInstanceService instance(logger, vulkanPolicy);
    if (!instance.IsValid())
    {
        return 1;
    }

    VulkanSurfaceService surface(logger, instance, *window);
    if (!surface.IsValid())
    {
        return 1;
    }

    VulkanPhysicalDeviceService physicalDevice(logger, instance, vulkanPolicy, &surface);
    if (!physicalDevice.IsValid())
    {
        return 1;
    }

    VulkanDeviceService device(logger, physicalDevice, vulkanPolicy);
    if (!device.IsValid())
    {
        return 1;
    }

    VulkanQueueService queues(logger, device, physicalDevice, vulkanPolicy);
    if (!queues.IsValid())
    {
        return 1;
    }

    logger.Info("SDL3 Vulkan window is running. Close the window to exit.");

    while (windows.IsAlive(windowId))
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);
        }

        SDL_Delay(16);
    }

    return 0;
}
