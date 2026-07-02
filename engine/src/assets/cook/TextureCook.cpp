#include <assets/cook/TextureCook.h>
#include <assets/cook/TextureImportSettings.h>

#include <assets/texture/TextureSerializer.h>
#include <render/Image.h>
#include <render/ImageLoader.h>

#include <math.h>
#include <string.h>

#include <bc7enc.h>
#include <rgbcx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace
{
    // -- Colorspace ----------------------------------------------------------

    float SrgbToLinear(float c)
    {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    float LinearToSrgb(float c)
    {
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    uint8_t FloatToByte(float c)
    {
        return static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
    }

    // -- Downsampling ----------------------------------------------------------
    //
    // 2x2 box filter, clamped at odd edges. Three channel disciplines
    // (Decision L): sRGB channels linearize -> average -> re-encode (alpha is
    // always linear); normal maps average as vectors and renormalize; linear
    // data averages directly.

    enum class MipFilter
    {
        Linear,
        Srgb,
        Normal,
    };

    void DownsampleLevel(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                         uint8_t* dst, uint32_t dstW, uint32_t dstH,
                         MipFilter filter)
    {
        for (uint32_t y = 0; y < dstH; ++y)
        {
            const uint32_t y0 = std::min(y * 2, srcH - 1);
            const uint32_t y1 = std::min(y * 2 + 1, srcH - 1);
            for (uint32_t x = 0; x < dstW; ++x)
            {
                const uint32_t x0 = std::min(x * 2, srcW - 1);
                const uint32_t x1 = std::min(x * 2 + 1, srcW - 1);

                const uint8_t* s00 = src + (uint64_t(y0) * srcW + x0) * 4;
                const uint8_t* s10 = src + (uint64_t(y0) * srcW + x1) * 4;
                const uint8_t* s01 = src + (uint64_t(y1) * srcW + x0) * 4;
                const uint8_t* s11 = src + (uint64_t(y1) * srcW + x1) * 4;
                uint8_t* d = dst + (uint64_t(y) * dstW + x) * 4;

                if (filter == MipFilter::Normal)
                {
                    // Decode [0,255] -> [-1,1], average, renormalize.
                    float v[3];
                    for (int c = 0; c < 3; ++c)
                    {
                        const float sum = float(s00[c]) + float(s10[c]) + float(s01[c]) + float(s11[c]);
                        v[c] = (sum / 4.0f) / 127.5f - 1.0f;
                    }
                    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    if (len > 1e-6f)
                    {
                        v[0] /= len; v[1] /= len; v[2] /= len;
                    }
                    else
                    {
                        v[0] = 0.0f; v[1] = 0.0f; v[2] = 1.0f; // degenerate -> flat +Z
                    }
                    for (int c = 0; c < 3; ++c)
                        d[c] = FloatToByte((v[c] + 1.0f) * 0.5f);
                    d[3] = static_cast<uint8_t>(
                        (uint32_t(s00[3]) + s10[3] + s01[3] + s11[3] + 2) / 4);
                    continue;
                }

                for (int c = 0; c < 4; ++c)
                {
                    // Alpha is linear even in sRGB images.
                    if (filter == MipFilter::Srgb && c < 3)
                    {
                        const float linear =
                            (SrgbToLinear(s00[c] / 255.0f) + SrgbToLinear(s10[c] / 255.0f)
                           + SrgbToLinear(s01[c] / 255.0f) + SrgbToLinear(s11[c] / 255.0f)) / 4.0f;
                        d[c] = FloatToByte(LinearToSrgb(linear));
                    }
                    else
                    {
                        d[c] = static_cast<uint8_t>(
                            (uint32_t(s00[c]) + s10[c] + s01[c] + s11[c] + 2) / 4);
                    }
                }
            }
        }
    }

    MipFilter FilterForUsage(TextureUsage usage)
    {
        switch (usage)
        {
        case TextureUsage::BaseColor:
        case TextureUsage::Emissive:
            return MipFilter::Srgb;
        case TextureUsage::Normal:
            return MipFilter::Normal;
        default:
            return MipFilter::Linear;
        }
    }

    bool HasSuffix(std::string_view stem, std::string_view suffix)
    {
        return stem.ends_with(suffix);
    }
} // namespace

TextureUsage InferTextureUsageFromName(std::string_view sourceRelPath)
{
    // Strip directory and extension to get the stem.
    std::string_view stem = sourceRelPath;
    if (const auto slash = stem.find_last_of('/'); slash != std::string_view::npos)
        stem = stem.substr(slash + 1);
    if (const auto dot = stem.find_last_of('.'); dot != std::string_view::npos)
        stem = stem.substr(0, dot);

    if (HasSuffix(stem, "_n") || HasSuffix(stem, "_nrm") || HasSuffix(stem, "_normal"))
        return TextureUsage::Normal;
    if (HasSuffix(stem, "_orm"))
        return TextureUsage::Orm;
    if (HasSuffix(stem, "_em") || HasSuffix(stem, "_emissive"))
        return TextureUsage::Emissive;
    if (HasSuffix(stem, "_mask") || HasSuffix(stem, "_lin") || HasSuffix(stem, "_data"))
        return TextureUsage::LinearData;
    return TextureUsage::BaseColor;
}

bool BuildTextureMipChainRgba8(const Image& image, TextureUsage usage, TextureData& out, std::string* error)
{
    if (!image.IsValid())
    {
        if (error)
            *error = "texture cook: invalid source image";
        return false;
    }
    if (usage == TextureUsage::Unknown)
    {
        if (error)
            *error = "texture cook: unknown texture usage";
        return false;
    }

    const bool srgb = usage == TextureUsage::BaseColor || usage == TextureUsage::Emissive;
    const MipFilter filter = FilterForUsage(usage);

    TextureData texture;
    texture.Format = srgb ? TexturePixelFormat::RGBA8_SRGB : TexturePixelFormat::RGBA8;
    texture.Usage = usage;
    texture.Width = image.Width;
    texture.Height = image.Height;

    const uint32_t levels = FullMipChainLength(image.Width, image.Height);

    // Lay out the mip table, then fill the blob level by level.
    uint64_t totalBytes = 0;
    uint32_t w = image.Width;
    uint32_t h = image.Height;
    texture.Mips.reserve(levels);
    for (uint32_t i = 0; i < levels; ++i)
    {
        const uint64_t size = TextureMipByteSize(texture.Format, w, h);
        texture.Mips.push_back(TextureMipLevel{ w, h, totalBytes, size });
        totalBytes += size;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    texture.Blob.resize(totalBytes);
    std::memcpy(texture.Blob.data(), image.Pixels.data(), texture.Mips[0].ByteSize);

    for (uint32_t i = 1; i < levels; ++i)
    {
        const TextureMipLevel& srcMip = texture.Mips[i - 1];
        const TextureMipLevel& dstMip = texture.Mips[i];
        DownsampleLevel(texture.Blob.data() + srcMip.Offset, srcMip.Width, srcMip.Height,
                        texture.Blob.data() + dstMip.Offset, dstMip.Width, dstMip.Height,
                        filter);
    }

    out = std::move(texture);
    return true;
}

namespace
{
    // Gathers one 4x4 block of RGBA pixels, clamping at the right/bottom
    // edges so sub-block mips (2x2, 1x1) and non-multiple-of-4 extents
    // replicate their edge texels.
    void GatherBlock(const uint8_t* pixels, uint32_t width, uint32_t height,
                     uint32_t blockX, uint32_t blockY, uint8_t out[16 * 4])
    {
        for (uint32_t y = 0; y < 4; ++y)
        {
            const uint32_t srcY = std::min(blockY * 4 + y, height - 1);
            for (uint32_t x = 0; x < 4; ++x)
            {
                const uint32_t srcX = std::min(blockX * 4 + x, width - 1);
                const uint8_t* src = pixels + (uint64_t(srcY) * width + srcX) * 4;
                std::memcpy(out + (y * 4 + x) * 4, src, 4);
            }
        }
    }

    void EnsureEncodersInitialized()
    {
        static const bool initialized = []
        {
            rgbcx::init();
            bc7enc_compress_block_init();
            return true;
        }();
        (void)initialized;
    }

    // Compresses an RGBA8 chain into `format`, level by level, block by
    // block. The mip table is recomputed for the block-compressed sizes.
    bool EncodeMipChain(const TextureData& rgba, TexturePixelFormat format, TextureData& out)
    {
        EnsureEncodersInitialized();

        bc7enc_compress_block_params bc7Params;
        bc7enc_compress_block_params_init(&bc7Params);

        TextureData encoded;
        encoded.Format = format;
        encoded.Usage = rgba.Usage;
        encoded.Width = rgba.Width;
        encoded.Height = rgba.Height;

        uint64_t totalBytes = 0;
        encoded.Mips.reserve(rgba.Mips.size());
        for (const TextureMipLevel& mip : rgba.Mips)
        {
            const uint64_t size = TextureMipByteSize(format, mip.Width, mip.Height);
            encoded.Mips.push_back(TextureMipLevel{ mip.Width, mip.Height, totalBytes, size });
            totalBytes += size;
        }
        encoded.Blob.resize(totalBytes);

        for (std::size_t level = 0; level < rgba.Mips.size(); ++level)
        {
            const TextureMipLevel& src = rgba.Mips[level];
            const TextureMipLevel& dst = encoded.Mips[level];
            const uint8_t* srcPixels = rgba.Blob.data() + src.Offset;
            uint8_t* dstBlocks = encoded.Blob.data() + dst.Offset;

            const uint32_t blocksX = (src.Width + 3) / 4;
            const uint32_t blocksY = (src.Height + 3) / 4;
            for (uint32_t by = 0; by < blocksY; ++by)
            {
                for (uint32_t bx = 0; bx < blocksX; ++bx)
                {
                    uint8_t block[16 * 4];
                    GatherBlock(srcPixels, src.Width, src.Height, bx, by, block);

                    const uint64_t blockIndex = uint64_t(by) * blocksX + bx;
                    switch (format)
                    {
                    case TexturePixelFormat::BC7:
                    case TexturePixelFormat::BC7_SRGB:
                        bc7enc_compress_block(dstBlocks + blockIndex * 16, block, &bc7Params);
                        break;
                    case TexturePixelFormat::BC5:
                        // X/Y in the two channels; Z is reconstructed
                        // in-shader (Decision L).
                        rgbcx::encode_bc5(dstBlocks + blockIndex * 16, block,
                                          /*chan0*/ 0, /*chan1*/ 1);
                        break;
                    case TexturePixelFormat::BC4:
                        rgbcx::encode_bc4(dstBlocks + blockIndex * 8, block);
                        break;
                    default:
                        return false;
                    }
                }
            }
        }

        out = std::move(encoded);
        return true;
    }
} // namespace

TexturePixelFormat CookedFormatForUsage(TextureUsage usage)
{
    switch (usage)
    {
    case TextureUsage::BaseColor:
    case TextureUsage::Emissive:
        return TexturePixelFormat::BC7_SRGB;
    case TextureUsage::Normal:
        return TexturePixelFormat::BC5;
    case TextureUsage::Orm:
    case TextureUsage::LinearData:
        // LinearData takes BC7 until a source format carries channel count;
        // PNG decode is always RGBA (Decision L's "BC4 by channel count").
        return TexturePixelFormat::BC7;
    default:
        return TexturePixelFormat::Unknown;
    }
}

bool CookImageToTexture(const Image& image, TextureUsage usage, TextureData& out, std::string* error)
{
    return CookImageToTexture(image, TextureCookParams{ .Usage = usage }, out, error);
}

bool CookImageToTexture(const Image& image,
                        const TextureCookParams& params,
                        TextureData& out,
                        std::string* error)
{
    TextureData rgba;
    if (!BuildTextureMipChainRgba8(image, params.Usage, rgba, error))
        return false;

    if (!params.GenerateMips)
    {
        rgba.Mips.resize(1);
        rgba.Blob.resize(rgba.Mips[0].ByteSize);
    }

    if (!params.Compress)
    {
        // Keep the colorspace-correct RGBA8/RGBA8_SRGB chain as-is.
        rgba.Filter = params.Filter;
        out = std::move(rgba);
        return true;
    }

    const TexturePixelFormat format = CookedFormatForUsage(params.Usage);
    if (format == TexturePixelFormat::Unknown)
    {
        if (error)
            *error = "texture cook: no cooked format for usage";
        return false;
    }

    TextureData encoded;
    if (!EncodeMipChain(rgba, format, encoded))
    {
        if (error)
            *error = "texture cook: block compression failed";
        return false;
    }

    encoded.Filter = params.Filter;
    out = std::move(encoded);
    return true;
}

std::vector<std::string_view> PngTextureImporter::SourceExtensions() const
{
    return { ".png" };
}

ImportResult PngTextureImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    const std::optional<Image> image = LoadImageFromMemory(
        reinterpret_cast<const uint8_t*>(input.Bytes.data()),
        static_cast<int>(input.Bytes.size()),
        /*srgb*/ true);
    if (!image)
        return ImportResult{ .Error = "png import: decode failed" };

    // Sidecar import settings (the driver reads the file); a malformed
    // sidecar fails the import rather than silently cooking with defaults.
    TextureImportSettings settings;
    std::string settingsError;
    if (!ParseTextureImportSettings(input.MetaBytes, settings, &settingsError))
        return ImportResult{ .Error = "png import: " + settingsError };

    const TextureCookParams params{
        .Usage = settings.Usage != TextureUsage::Unknown
                     ? settings.Usage
                     : InferTextureUsageFromName(input.SourceRelPath),
        .Filter = settings.Filter,
        .Compress = settings.Compress,
        .GenerateMips = settings.GenerateMips,
    };

    TextureData texture;
    std::string cookError;
    if (!CookImageToTexture(*image, params, texture, &cookError))
        return ImportResult{ .Error = cookError };

    std::vector<std::byte> stexBytes;
    if (!WriteStexToBytes(texture, stexBytes))
        return ImportResult{ .Error = "png import: stex serialization failed" };

    CookedArtifact artifact;
    artifact.Path = "asset://" + std::string(input.SourceRelPath);
    artifact.FileRelPath = ".cooked/" + std::string(input.SourceRelPath) + ".stex";
    artifact.Type = AssetType::Texture;

    if (!output.WriteBytes(artifact.FileRelPath, stexBytes))
        return ImportResult{ .Error = "png import: artifact write failed" };

    ImportResult result;
    result.Artifacts.push_back(std::move(artifact));
    return result;
}
