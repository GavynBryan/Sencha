#pragma once

#include <cstdint>

//=============================================================================
// .sui container
//
// Header, then four sequential sections of length-prefixed records: blobs,
// resources, unsupported-capability notes, and the blob bytes. Sequential
// rather than offset-tabled because every field here is variable length and a
// package is read once when a document opens, never walked per frame -- the
// .stex offset table earns its complexity by being indexed into, and this is
// not.
//
// Deterministic: the cooker emits records in a fixed order, so the same sources
// produce the same bytes and the cooked-cache hash is stable.
//=============================================================================

inline constexpr char kSuiMagic[4] = { 'S', 'U', 'I', ' ' };
inline constexpr std::uint32_t kSuiVersion = 1;

// Header flag bits. Absent flags read as the pre-flag behaviour, so adding one
// never needs a version bump.
inline constexpr std::uint32_t kSuiFlagNone = 0u;

struct SuiFileHeader
{
    char Magic[4];
    std::uint32_t Version = 0;

    std::uint32_t Flags = 0;
    std::uint32_t HeaderSize = 0;

    std::uint32_t BlobCount = 0;
    std::uint32_t ResourceCount = 0;
    std::uint32_t UnsupportedCount = 0;
    std::uint32_t Reserved0 = 0;
};

static_assert(sizeof(SuiFileHeader) == 32);

// True if `bytes` begin with the .sui magic. The asset layer sniffs container
// magic rather than trusting extensions: a cooked artifact keeps its source's
// virtual path, so the path may say ".rml" while the bytes are a cooked .sui.
[[nodiscard]] inline bool LooksLikeSui(const void* bytes, std::uint64_t size)
{
    if (size < sizeof(SuiFileHeader))
        return false;
    const char* p = static_cast<const char*>(bytes);
    return p[0] == kSuiMagic[0] && p[1] == kSuiMagic[1]
        && p[2] == kSuiMagic[2] && p[3] == kSuiMagic[3];
}
