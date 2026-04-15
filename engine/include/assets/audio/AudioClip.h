#pragma once

#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// AudioClip
//
// CPU-side audio asset. Holds raw PCM samples decoded from a source file.
// The clip is backend-agnostic; it knows nothing about SDL streams, buses,
// or voices. AudioService retrieves clips from the asset cache by id.
//
// Samples are interleaved: for stereo at 44100 Hz, Samples contains
//   [L0, R0, L1, R1, ...] Sint16 values.
//
// A zero ChannelCount or SampleRate means the clip is invalid. Check
// IsValid() before handing a clip to AudioService.
//=============================================================================
struct AudioClip
{
    std::vector<int16_t> Samples;
    uint32_t             SampleRate   = 0;
    uint8_t              ChannelCount = 0;

    [[nodiscard]] bool     IsValid()      const { return !Samples.empty() && SampleRate > 0 && ChannelCount > 0; }
    [[nodiscard]] uint32_t FrameCount()   const { return ChannelCount > 0 ? static_cast<uint32_t>(Samples.size()) / ChannelCount : 0; }
    [[nodiscard]] uint32_t ByteSize()     const { return static_cast<uint32_t>(Samples.size()) * sizeof(int16_t); }
};
