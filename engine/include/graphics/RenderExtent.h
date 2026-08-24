#pragma once

#include <cstdint>

//=============================================================================
// RenderExtent
//
// Pixel dimensions of a render target. Exists so render-domain code that only
// needs a width and a height -- camera aspect, viewport sizing, target
// requests -- does not pull the Vulkan headers in for VkExtent2D. Backends
// convert at their own boundary.
//
// A zero extent is the "not yet sized" state: consumers reject it rather than
// dividing by it.
//=============================================================================
struct RenderExtent
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;

    [[nodiscard]] bool IsEmpty() const { return Width == 0 || Height == 0; }

    [[nodiscard]] friend bool operator==(RenderExtent, RenderExtent) = default;
};
