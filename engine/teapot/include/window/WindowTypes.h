#pragma once

#include <cstdint>

enum class WindowMode : uint8_t
{
    Windowed,
    Fullscreen,
    BorderlessFullscreen
};

enum class WindowGraphicsApi : uint8_t
{
    None,
    Vulkan
};

struct WindowExtent
{
    uint32_t Width  = 0;
    uint32_t Height = 0;
};
