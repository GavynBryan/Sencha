#pragma once

#include <cstdint>

// Backend-neutral value types shared across the render hardware interface.
// Types here name mechanisms in API-independent terms so the render layer can
// use them without seeing any backend header. This is the seed of the rhi
// layer; further neutral types (Format, the pipeline descriptors, the command
// list) land as the migration proceeds.

namespace rhi
{

// Pixel dimensions of a render target or surface. Zero in either field means
// no valid target (minimized window, unconfigured swapchain).
struct Extent2D
{
    uint32_t Width = 0;
    uint32_t Height = 0;

    bool operator==(const Extent2D&) const = default;
};

} // namespace rhi
