#pragma once

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
// BytesPerPixel() is always 4 and ByteSize() == Width * Height * 4.
//=============================================================================
struct Image
{
    std::vector<uint8_t> Pixels;
    uint32_t Width  = 0;
    uint32_t Height = 0;
    PixelFormat Format = PixelFormat::RGBA8_SRGB;

    [[nodiscard]] bool     IsValid()       const { return !Pixels.empty() && Width > 0 && Height > 0; }
    [[nodiscard]] uint32_t BytesPerPixel() const { return 4; }
    [[nodiscard]] uint32_t ByteSize()      const { return Width * Height * BytesPerPixel(); }
};
