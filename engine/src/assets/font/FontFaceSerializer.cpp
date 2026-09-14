#include <assets/font/FontFaceSerializer.h>

#include <assets/font/FontFaceFormat.h>

#include <cstring>
#include <limits>

namespace
{
void SetError(std::string* error, const char* message)
{
    if (error != nullptr)
        *error = message;
}
} // namespace

bool WriteSfontToBytes(const FontFace& face, std::vector<std::byte>& out)
{
    if (!face.IsValid())
        return false;
    if (face.Family.size() > std::numeric_limits<std::uint32_t>::max())
        return false;

    SfontFileHeader header{};
    std::memcpy(header.Magic, kSfontMagic, sizeof(header.Magic));
    header.Version = kSfontVersion;
    header.Flags = face.Fallback ? kSfontFlagFallback : 0u;
    header.HeaderSize = sizeof(SfontFileHeader);
    header.Style = static_cast<std::uint16_t>(face.Style);
    header.Weight = face.Weight;
    header.FaceByteCount = face.Bytes.size();

    const auto familyLength = static_cast<std::uint32_t>(face.Family.size());

    out.clear();
    out.resize(sizeof(SfontFileHeader) + sizeof(familyLength)
               + face.Family.size() + face.Bytes.size());

    std::size_t cursor = 0;
    std::memcpy(out.data() + cursor, &header, sizeof(header));
    cursor += sizeof(header);
    std::memcpy(out.data() + cursor, &familyLength, sizeof(familyLength));
    cursor += sizeof(familyLength);
    std::memcpy(out.data() + cursor, face.Family.data(), face.Family.size());
    cursor += face.Family.size();
    std::memcpy(out.data() + cursor, face.Bytes.data(), face.Bytes.size());
    return true;
}

bool LoadSfontFromBytes(std::span<const std::byte> bytes, FontFace& out, std::string* error)
{
    if (!LooksLikeSfont(bytes.data(), bytes.size()))
    {
        SetError(error, "not a .sfont container");
        return false;
    }

    SfontFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.Version != kSfontVersion)
    {
        SetError(error, "unsupported .sfont version");
        return false;
    }
    if (header.HeaderSize < sizeof(SfontFileHeader) || header.HeaderSize > bytes.size())
    {
        SetError(error, "implausible .sfont header size");
        return false;
    }
    if (header.Style > static_cast<std::uint16_t>(FontStyle::Italic))
    {
        SetError(error, "unknown .sfont style");
        return false;
    }

    // Seek by the recorded header size, so a later version that only grows the
    // header stays readable.
    std::size_t cursor = header.HeaderSize;
    std::uint32_t familyLength = 0;
    if (bytes.size() - cursor < sizeof(familyLength))
    {
        SetError(error, "truncated .sfont family length");
        return false;
    }
    std::memcpy(&familyLength, bytes.data() + cursor, sizeof(familyLength));
    cursor += sizeof(familyLength);

    if (familyLength == 0 || familyLength > bytes.size() - cursor)
    {
        SetError(error, "truncated or empty .sfont family");
        return false;
    }
    FontFace parsed;
    parsed.Family.assign(reinterpret_cast<const char*>(bytes.data() + cursor), familyLength);
    cursor += familyLength;

    if (header.FaceByteCount == 0 || header.FaceByteCount > bytes.size() - cursor)
    {
        SetError(error, ".sfont face bytes disagree with the header");
        return false;
    }
    parsed.Bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                        bytes.begin() + static_cast<std::ptrdiff_t>(cursor + header.FaceByteCount));

    parsed.Style = static_cast<FontStyle>(header.Style);
    parsed.Weight = header.Weight;
    parsed.Fallback = (header.Flags & kSfontFlagFallback) != 0;

    out = std::move(parsed);
    return true;
}
