#pragma once

#include <cstdint>
#include <string>
#include <platform/WindowTypes.h>

struct WindowCreateInfo
{
    std::string Title    = "Sencha";
    uint32_t    Width    = 1280;
    uint32_t    Height   = 720;
    WindowMode  Mode     = WindowMode::Windowed;
    WindowGraphicsApi GraphicsApi = WindowGraphicsApi::None;
    bool        Resizable = true;
    bool        Visible   = true;
};
