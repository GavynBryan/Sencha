#pragma once

#include <cstdint>
#include <vector>

//=============================================================================
// TextureData (docs/assets/pipeline.md, Decisions E and L)
//
// The CPU-side cooked texture: a format-tagged, usage-tagged, mip-tabled
// pixel payload. This is the widened seam between the asset layer and the
// GPU upload path — consumers take (format, extent, mip table, byte span)
// and may never assume bytes-per-pixel or an uncompressed layout. `Image`
// remains the simple stb-decode result; TextureData is what `.stex`
// carries and what TextureCache uploads.
//
// Backend-free by design: no Vulkan types. The Vulkan mapping lives with
// TextureCache.
//=============================================================================

// Pixel formats a cooked texture may carry. Block-compressed entries are
// first-class from v1 so the compressed path can never rot into a de-facto
// hardcoded RGBA8 assumption (Decision E).
enum class TexturePixelFormat : uint16_t
{
    Unknown = 0,
    RGBA8 = 1,       // 4 B/px, linear
    RGBA8_SRGB = 2,  // 4 B/px, sRGB sampled
    BC4 = 3,         // 8 B per 4x4 block, single channel, linear
    BC5 = 4,         // 16 B per 4x4 block, two channels, linear (normals)
    BC7 = 5,         // 16 B per 4x4 block, linear
    BC7_SRGB = 6,    // 16 B per 4x4 block, sRGB sampled
};

// What the texture is *for* (Decision L). Usage determines colorspace and
// the cook step's compression choice; no consumer ever guesses either.
enum class TextureUsage : uint16_t
{
    Unknown = 0,
    BaseColor = 1,   // sRGB
    Normal = 2,      // linear, renormalized per mip at cook
    Orm = 3,         // linear; R=occlusion G=roughness B=metallic
    Emissive = 4,    // sRGB
    LinearData = 5,  // linear; masks, misc. data
};

// How the texture asks to be sampled. Authored per source (import settings)
// and carried through the .stex so the runtime picks the sampler from data;
// Nearest is the pixel-art path (point-filtered at every stage).
enum class TextureFilter : uint16_t
{
    Linear = 0,
    Nearest = 1,
};

struct TextureMipLevel
{
    uint32_t Width = 0;
    uint32_t Height = 0;

    // Byte range within TextureData::Blob.
    uint64_t Offset = 0;
    uint64_t ByteSize = 0;
};

struct TextureData
{
    TexturePixelFormat Format = TexturePixelFormat::Unknown;
    TextureUsage Usage = TextureUsage::Unknown;
    TextureFilter Filter = TextureFilter::Linear;

    // Mip 0 extent. Mips[0] must agree.
    uint32_t Width = 0;
    uint32_t Height = 0;

    // Tightly packed mip payloads, largest first.
    std::vector<TextureMipLevel> Mips;
    std::vector<uint8_t> Blob;

    [[nodiscard]] bool IsValid() const
    {
        return Format != TexturePixelFormat::Unknown && Width > 0 && Height > 0
            && !Mips.empty() && !Blob.empty();
    }
};

[[nodiscard]] constexpr bool TextureFormatIsSrgb(TexturePixelFormat format)
{
    return format == TexturePixelFormat::RGBA8_SRGB
        || format == TexturePixelFormat::BC7_SRGB;
}

[[nodiscard]] constexpr bool TextureFormatIsBlockCompressed(TexturePixelFormat format)
{
    return format == TexturePixelFormat::BC4
        || format == TexturePixelFormat::BC5
        || format == TexturePixelFormat::BC7
        || format == TexturePixelFormat::BC7_SRGB;
}

// Byte size of one mip level at `width` x `height`. Returns 0 for Unknown.
[[nodiscard]] constexpr uint64_t TextureMipByteSize(TexturePixelFormat format,
                                                    uint32_t width,
                                                    uint32_t height)
{
    switch (format)
    {
    case TexturePixelFormat::RGBA8:
    case TexturePixelFormat::RGBA8_SRGB:
        return uint64_t(width) * height * 4;
    case TexturePixelFormat::BC4:
        return uint64_t((width + 3) / 4) * ((height + 3) / 4) * 8;
    case TexturePixelFormat::BC5:
    case TexturePixelFormat::BC7:
    case TexturePixelFormat::BC7_SRGB:
        return uint64_t((width + 3) / 4) * ((height + 3) / 4) * 16;
    default:
        return 0;
    }
}

// Number of levels in a full mip chain down to 1x1.
[[nodiscard]] constexpr uint32_t FullMipChainLength(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    uint32_t size = width > height ? width : height;
    while (size > 1)
    {
        size /= 2;
        ++levels;
    }
    return levels;
}

// Validates the structural invariants the runtime relies on: mip 0 matches
// the header extent, every level's byte size matches its format and extent,
// extents halve level to level (floor, min 1), ranges are contiguous from
// offset 0, and the blob is exactly covered.
[[nodiscard]] bool ValidateTextureData(const TextureData& texture);
