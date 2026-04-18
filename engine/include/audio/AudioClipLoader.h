#pragma once

#include <audio/AudioClip.h>

#include <optional>
#include <string_view>

//=============================================================================
// LoadAudioClipFromFile
//
// Loads a WAV file from disk via SDL_LoadWAV and converts the decoded audio
// to Sint16 interleaved PCM, normalising to the clip's native sample rate and
// channel count. No resampling is performed; the clip carries whatever rate
// the file declares and AudioService creates an SDL_AudioStream to resample
// on playback.
//
// Returns nullopt if the file cannot be opened or decoded. The caller is
// responsible for logging the failure -- this function is intentionally silent.
//=============================================================================
[[nodiscard]] std::optional<AudioClip> LoadAudioClipFromFile(std::string_view path);
