#include <logging/ConsoleLogSink.h>
#include <sdl/SdlVulkanSurfaceProvider.h>
#include <sdl/SdlWindow.h>
#include <service/ServiceHost.h>
#include <vulkan/VulkanInstanceService.h>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

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
    windowInfo.Resizable = true;
    windowInfo.Visible = true;

    SdlWindow window(logger, windowInfo);
    if (!window.IsValid())
    {
        return 1;
    }

    SdlVulkanSurfaceProvider surfaceProvider(logger, window);

    VulkanInstanceService::CreateInfo instanceInfo;
    instanceInfo.AppName = "SdlVulkanWindow";

    VulkanInstanceService instance(logger, instanceInfo, &surfaceProvider);
    if (!instance.IsValid())
    {
        return 1;
    }

    auto surfaceResult = surfaceProvider.CreateSurface(instance.GetInstance());
    if (!surfaceResult.Succeeded())
    {
        logger.Error("Failed to create Vulkan surface: {}", surfaceResult.Error);
        return 1;
    }

    logger.Info("SDL3 Vulkan window is running. Close the window to exit.");

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
        }

        SDL_Delay(16);
    }

    vkDestroySurfaceKHR(instance.GetInstance(), surfaceResult.Surface, nullptr);
    logger.Info("Vulkan surface destroyed");

    return 0;
}
