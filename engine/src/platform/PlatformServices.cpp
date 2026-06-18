#include <platform/PlatformServices.h>

#include <platform/SdlWindow.h>
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

SdlWindow* PlatformServices::CreatePrimaryWindow(const EngineWindowConfig& config)
{
    return Windows.CreateWindow(BuildWindowCreateInfo(config));
}
