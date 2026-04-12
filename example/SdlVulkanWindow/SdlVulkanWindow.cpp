#include <logging/ConsoleLogSink.h>
#include <sdl/SdlWindow.h>
#include <service/ServiceHost.h>
#include <vulkan/VulkanDeviceService.h>
#include <vulkan/VulkanInstanceService.h>

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

    SdlWindow window(logger, windowInfo);
    if (!window.IsValid())
    {
        return 1;
    }

    VulkanInstanceService::CreateInfo instanceInfo;
    instanceInfo.AppName = "SdlVulkanWindow";

    VulkanInstanceService instance(logger, instanceInfo, &window);
    if (!instance.IsValid())
    {
        return 1;
    }

    VulkanDeviceService::CreateInfo deviceInfo;
    VulkanDeviceService device(logger, instance, deviceInfo, &window);
    if (!device.IsValid())
    {
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

    return 0;
}
