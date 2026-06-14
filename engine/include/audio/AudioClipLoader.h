#pragma once

#include <audio/AudioClip.h>

#include <cstddef>
#include <optional>
#include <span>

//=============================================================================
// LoadAudioClipFromWavBytes
//
// Decodes an in-memory WAV file via SDL_LoadWAV_IO and converts the decoded
// audio to Sint16 interleaved PCM. No resampling is performed; the clip
// carries whatever rate the file declares and AudioService creates an
// SDL_AudioStream to resample on playback.
//
// Bytes in, clip out (docs/assets/pipeline.md, Decision I): pure with
// respect to engine state, so it is callable from asset-loader stage halves
// on task threads and from the WAV importer at cook.
//
// Returns nullopt if the bytes cannot be decoded. The caller is responsible
// for reporting the failure -- this function is intentionally silent.
//=============================================================================
[[nodiscard]] std::optional<AudioClip> LoadAudioClipFromWavBytes(std::span<const std::byte> bytes);
