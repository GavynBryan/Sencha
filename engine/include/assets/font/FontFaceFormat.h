#pragma once

#include <cstdint>

//=============================================================================
// .sfont container
//
// A cooked font is the face bytes plus the registration metadata a document
// engine needs to file them under: family, style, weight, fallback. The face
// itself is passed through unchanged -- there is no glyph baking here, because
// the atlas belongs to whichever runtime rasterises it at the size a document
// actually asks for, not to the cook.
//
// Header, then the family string (length-prefixed), then the face bytes.
//=============================================================================

inline constexpr char kSfontMagic[4] = { 'S', 'F', 'N', 'T' };
inline constexpr std::uint32_t kSfontVersion = 1;

inline constexpr std::uint32_t kSfontFlagFallback = 1u << 0;

struct SfontFileHeader
{
    char Magic[4];
    std::uint32_t Version = 0;

    std::uint32_t Flags = 0;
    std::uint32_t HeaderSize = 0;

    std::uint16_t Style = 0;
    std::uint16_t Weight = 0;
    std::uint32_t Reserved0 = 0;

    std::uint64_t FaceByteCount = 0;
};

static_assert(sizeof(SfontFileHeader) == 32);

[[nodiscard]] inline bool LooksLikeSfont(const void* bytes, std::uint64_t size)
{
    if (size < sizeof(SfontFileHeader))
        return false;
    const char* p = static_cast<const char*>(bytes);
    return p[0] == kSfontMagic[0] && p[1] == kSfontMagic[1]
        && p[2] == kSfontMagic[2] && p[3] == kSfontMagic[3];
}
