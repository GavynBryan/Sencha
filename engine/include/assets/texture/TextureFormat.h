#pragma once

#include <render/TextureData.h>

#include <cstdint>

//=============================================================================
// .stex container (docs/assets/pipeline.md, Decisions E and L)
//
// Format-tagged from its first version: an explicit pixel-format tag, a
// usage tag, and a mip table with per-level offsets and byte sizes. No
// consumer may assume bytes-per-pixel or an uncompressed layout.
//
// Layout: StexFileHeader, then MipCount StexMipRecords, then the packed
// pixel blob. Mip records are ordered largest first; offsets are relative
// to PixelDataOffset and must be contiguous from 0.
//=============================================================================

inline constexpr char kStexMagic[4] = { 'S', 'T', 'E', 'X' };
inline constexpr uint32_t kStexVersion = 1;

struct StexFileHeader
{
    char Magic[4];
    uint32_t Version = 0;

    uint32_t Flags = 0;
    uint32_t Reserved0 = 0;

    TexturePixelFormat Format = TexturePixelFormat::Unknown;
    TextureUsage Usage = TextureUsage::Unknown;

    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t MipCount = 0;

    uint32_t HeaderSize = 0;
    uint32_t MipTableOffset = 0;
    uint32_t PixelDataOffset = 0;
    uint64_t PixelDataSize = 0;
};

struct StexMipRecord
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint64_t Offset = 0;   // relative to PixelDataOffset
    uint64_t ByteSize = 0;
};

static_assert(sizeof(StexFileHeader) == 56);
static_assert(sizeof(StexMipRecord) == 24);

// True if `bytes` begin with the .stex magic. The asset layer sniffs
// container magic instead of trusting extensions — a cooked artifact keeps
// its source's virtual path (Decision B), so the path may say ".png" while
// the bytes are a cooked .stex.
[[nodiscard]] inline bool LooksLikeStex(const void* bytes, uint64_t size)
{
    if (size < sizeof(StexFileHeader))
        return false;
    const char* p = static_cast<const char*>(bytes);
    return p[0] == kStexMagic[0] && p[1] == kStexMagic[1]
        && p[2] == kStexMagic[2] && p[3] == kStexMagic[3];
}
