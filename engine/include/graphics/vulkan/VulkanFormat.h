#pragma once

#include <vulkan/vulkan.h>

// Whether a depth format carries a stencil aspect. Authored UI clips to rounded
// boundaries through the stencil; an image created without the aspect, or a
// barrier or attachment that names only depth, silently degrades that clipping
// to a rectangle. One answer, so the depth target, the target store and the
// target session cannot disagree about which formats those are.
[[nodiscard]] constexpr bool FormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT
        || format == VK_FORMAT_D24_UNORM_S8_UINT
        || format == VK_FORMAT_D16_UNORM_S8_UINT;
}

// Whether a display target stores encoded values with nothing to encode them on
// write. The frame encodes once: an _SRGB target does it in hardware, a float one
// holds linear values for whoever samples it, and only these need the shader to.
[[nodiscard]] constexpr bool FormatNeedsShaderEncode(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM
        || format == VK_FORMAT_R8G8B8A8_UNORM
        || format == VK_FORMAT_A8B8G8R8_UNORM_PACK32
        || format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
        || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
}
