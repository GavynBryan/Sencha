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

// Whether writing linear light to this colour format encodes it in hardware. The
// frame encodes once: a display target that does not gets it from the output pass.
[[nodiscard]] constexpr bool FormatEncodesSrgbOnWrite(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_SRGB
        || format == VK_FORMAT_R8G8B8A8_SRGB
        || format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}
