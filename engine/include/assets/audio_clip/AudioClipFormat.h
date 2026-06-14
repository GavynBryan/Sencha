#pragma once

#include <cstdint>

//=============================================================================
// .sclip container (docs/assets/pipeline.md, Decisions B and F)
//
// The cooked runtime format for resident audio clips: a fixed header
// followed by interleaved Sint16 PCM, little-endian. Decode happens at cook
// (WAV/OGG → PCM); the runtime parse is a validated copy, which is what
// makes the audio stage half pure and task-thread-safe.
//
// This container carries *clips* — fully resident, decode-once SFX.
// Streamed music is a different contract entirely and gets its own format
// when the audio plan exists (Decision F); do not grow this header toward
// streaming.
//=============================================================================

inline constexpr char kSclipMagic[4] = { 'S', 'C', 'L', 'P' };
inline constexpr uint32_t kSclipVersion = 1;

struct SclipFileHeader
{
    char Magic[4];
    uint32_t Version = 0;

    uint32_t SampleRate = 0;
    uint32_t ChannelCount = 0;

    // Total Sint16 samples across all channels (frames * channels).
    uint64_t SampleCount = 0;

    uint32_t HeaderSize = 0;
    uint32_t SampleDataOffset = 0;
};

static_assert(sizeof(SclipFileHeader) == 32);

// True if `bytes` begin with the .sclip magic. The asset layer sniffs
// container magic instead of trusting extensions — a cooked artifact keeps
// its source's virtual path (Decision B), so the path may say ".wav" while
// the bytes are a cooked .sclip.
[[nodiscard]] inline bool LooksLikeSclip(const void* bytes, uint64_t size)
{
    if (size < sizeof(SclipFileHeader))
        return false;
    const char* p = static_cast<const char*>(bytes);
    return p[0] == kSclipMagic[0] && p[1] == kSclipMagic[1]
        && p[2] == kSclipMagic[2] && p[3] == kSclipMagic[3];
}
