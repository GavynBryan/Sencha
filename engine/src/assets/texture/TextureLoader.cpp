#include <assets/texture/TextureLoader.h>

#include <assets/texture/TextureFormat.h>

#include <cstring>

namespace
{
    bool Fail(std::string* error, const char* message)
    {
        if (error)
            *error = message;
        return false;
    }
} // namespace

bool LoadStexFromBytes(std::span<const std::byte> bytes, TextureData& out, std::string* error)
{
    if (bytes.size() < sizeof(StexFileHeader))
        return Fail(error, "stex: byte stream smaller than header");

    StexFileHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));

    if (std::memcmp(header.Magic, kStexMagic, sizeof(header.Magic)) != 0)
        return Fail(error, "stex: bad magic");
    if (header.Version != kStexVersion)
        return Fail(error, "stex: unsupported version");
    if (header.HeaderSize != sizeof(StexFileHeader))
        return Fail(error, "stex: unexpected header size");
    if (header.MipCount == 0)
        return Fail(error, "stex: zero mip count");

    const uint64_t mipTableEnd =
        uint64_t(header.MipTableOffset) + sizeof(StexMipRecord) * uint64_t(header.MipCount);
    if (header.MipTableOffset < header.HeaderSize || mipTableEnd > bytes.size())
        return Fail(error, "stex: mip table out of range");
    if (header.PixelDataOffset < mipTableEnd)
        return Fail(error, "stex: pixel data overlaps mip table");
    if (uint64_t(header.PixelDataOffset) + header.PixelDataSize > bytes.size())
        return Fail(error, "stex: pixel data out of range");

    TextureData texture;
    texture.Format = header.Format;
    texture.Usage = header.Usage;
    texture.Filter = (header.Flags & kStexFlagNearestFilter) != 0 ? TextureFilter::Nearest
                                                                  : TextureFilter::Linear;
    texture.Width = header.Width;
    texture.Height = header.Height;

    texture.Mips.resize(header.MipCount);
    for (uint32_t i = 0; i < header.MipCount; ++i)
    {
        StexMipRecord record;
        std::memcpy(&record,
                    bytes.data() + header.MipTableOffset + sizeof(StexMipRecord) * i,
                    sizeof(record));
        texture.Mips[i] = TextureMipLevel{
            .Width = record.Width,
            .Height = record.Height,
            .Offset = record.Offset,
            .ByteSize = record.ByteSize,
        };
    }

    texture.Blob.resize(header.PixelDataSize);
    if (header.PixelDataSize > 0)
        std::memcpy(texture.Blob.data(),
                    bytes.data() + header.PixelDataOffset,
                    header.PixelDataSize);

    if (!ValidateTextureData(texture))
        return Fail(error, "stex: structural validation failed");

    out = std::move(texture);
    return true;
}
