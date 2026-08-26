#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

//=============================================================================
// PixelFormat
//
// CPU-side pixel formats. Sencha forces RGBA output from stb_image so only
// 4-byte-per-pixel variants are listed. SRGB vs. linear matters to the GPU
// sampler -- color maps should use RGBA8_SRGB; data maps (normals, roughness)
// should use RGBA8.
//=============================================================================
enum class PixelFormat : uint8_t
{
    RGBA8,      // VK_FORMAT_R8G8B8A8_UNORM -- data/linear textures
    RGBA8_SRGB, // VK_FORMAT_R8G8B8A8_SRGB  -- color maps, sprites
};

//=============================================================================
// Image
//
// CPU-side pixel buffer. Vulkan-agnostic; knows nothing about ImageHandles,
// descriptors, or samplers. The asset layer (TextureCache) uploads an Image
// to the GPU and hands back a TextureHandle.
//
// stb_image always produces tightly-packed RGBA (4 bytes per pixel), so
// BytesPerPixel() is always 4.
//
// IsValid() is the buffer's size contract, not just a populated-ness check:
// consumers size copies out of Pixels from Width and Height, so a valid Image
// must own exactly Width * Height * 4 bytes.
//=============================================================================
struct Image
{
    std::vector<uint8_t> Pixels;
    uint32_t Width  = 0;
    uint32_t Height = 0;
    PixelFormat Format = PixelFormat::RGBA8_SRGB;

    [[nodiscard]] bool IsValid() const
    {
        if (Width == 0 || Height == 0)
            return false;
        // Compared in pixels rather than bytes: the product of two uint32_t
        // dimensions always fits in uint64_t, but that product times four does
        // not, so the byte form can wrap and admit a short buffer.
        return Pixels.size() % BytesPerPixel() == 0
            && Pixels.size() / BytesPerPixel() == uint64_t(Width) * uint64_t(Height);
    }

    [[nodiscard]] uint32_t BytesPerPixel() const { return 4; }

    // The storage actually owned, never a value recomputed from the dimensions.
    [[nodiscard]] std::size_t ByteSize() const { return Pixels.size(); }
};
