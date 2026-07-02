#include <assets/texture/TextureSerializer.h>

#include <assets/texture/TextureFormat.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryWriter.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
    bool WriteToWriter(BinaryWriter& writer, const TextureData& texture)
    {
        StexFileHeader header{};
        std::memcpy(header.Magic, kStexMagic, sizeof(header.Magic));
        header.Version = kStexVersion;
        if (texture.Filter == TextureFilter::Nearest)
            header.Flags |= kStexFlagNearestFilter;
        header.Format = texture.Format;
        header.Usage = texture.Usage;
        header.Width = texture.Width;
        header.Height = texture.Height;
        header.MipCount = static_cast<uint32_t>(texture.Mips.size());
        header.HeaderSize = sizeof(StexFileHeader);
        header.MipTableOffset = header.HeaderSize;
        header.PixelDataOffset = header.MipTableOffset
            + static_cast<uint32_t>(sizeof(StexMipRecord) * texture.Mips.size());
        header.PixelDataSize = texture.Blob.size();

        if (!writer.Write(header))
            return false;

        for (const TextureMipLevel& mip : texture.Mips)
        {
            StexMipRecord record{};
            record.Width = mip.Width;
            record.Height = mip.Height;
            record.Offset = mip.Offset;
            record.ByteSize = mip.ByteSize;
            if (!writer.Write(record))
                return false;
        }

        return writer.WriteBytes(
            reinterpret_cast<const char*>(texture.Blob.data()),
            static_cast<std::streamsize>(texture.Blob.size()));
    }
} // namespace

bool WriteStexToBytes(const TextureData& texture, std::vector<std::byte>& out)
{
    if (!ValidateTextureData(texture))
        return false;

    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    if (!WriteToWriter(writer, texture))
        return false;

    const std::string bytes = stream.str();
    out.resize(bytes.size());
    if (!bytes.empty())
        std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

TextureSerializer::TextureSerializer(LoggingProvider& logging)
    : Log(logging.GetLogger<TextureSerializer>())
{
}

bool TextureSerializer::WriteToFile(std::string_view path, const TextureData& texture)
{
    if (!ValidateTextureData(texture))
    {
        Log.Error("TextureSerializer rejected invalid texture data for '{}'", path);
        return false;
    }

    std::ofstream stream(std::string(path), std::ios::binary);
    if (!stream.is_open())
    {
        Log.Error("TextureSerializer: failed to open '{}' for write", path);
        return false;
    }

    BinaryWriter writer(stream);
    if (!WriteToWriter(writer, texture) || !stream.good())
    {
        Log.Error("TextureSerializer: failed to write '{}'", path);
        return false;
    }

    return true;
}

bool TextureSerializer::WriteToBytes(const TextureData& texture, std::vector<std::byte>& out)
{
    if (!WriteStexToBytes(texture, out))
    {
        Log.Error("TextureSerializer rejected invalid texture data");
        return false;
    }
    return true;
}
