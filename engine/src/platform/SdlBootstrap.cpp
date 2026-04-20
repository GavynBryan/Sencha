#include <platform/SdlBootstrap.h>

#include <core/logging/LoggingProvider.h>
#include <core/service/ServiceHost.h>
#include <platform/SdlVideoService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

namespace
{
    WindowCreateInfo BuildWindowCreateInfo(const EngineWindowConfig& config)
    {
        WindowCreateInfo info;
        info.Title = config.Title;
        info.Width = config.Width;
        info.Height = config.Height;
        info.Mode = config.Mode;
        info.GraphicsApi = config.GraphicsApi;
        info.Resizable = config.Resizable;
        info.Visible = config.Visible;
        return info;
    }
}

SdlWindow* SdlBootstrap::Install(ServiceHost& services,
                                 const EngineConfig& config,
                                 LoggingProvider& logging)
{
    auto& video = services.AddService<SdlVideoService>(logging);
    auto& windows = services.AddService<SdlWindowService>(logging, video);

    return windows.CreateWindow(BuildWindowCreateInfo(config.Window));
}
