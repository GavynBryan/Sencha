#pragma once

#include <audio/AudioVoice.h>    // VoiceId, VoiceState -- public types only
#include <core/identity/Id.h>

#include <SDL3/SDL_audio.h>
#include <cstdint>

//=============================================================================
// AudioVoiceSlot
//
// Private implementation struct owned exclusively by AudioService. Never
// exposed through the public API; callers hold VoiceIds instead.
//
// Stream is the SDL_AudioStream bound to the device for this voice. It is
// created in Play() and destroyed in RetireVoice().
//=============================================================================
struct AudioVoiceSlot
{
    AssetId          ClipId;
    uint32_t         BusIndex     = 0;
    uint32_t         Generation   = 0;

    VoiceState       State        = VoiceState::Idle;
    bool             Looping      = false;

    float            Gain         = 1.0f;  // [0, 1] per-voice attenuation
    float            Pan          = 0.0f;  // [-1 left, 0 center, +1 right]

    uint32_t         FrameCursor  = 0;
    uint32_t         StartTick    = 0;     // monotonic tick at allocation; used for StealOldest

    SDL_AudioStream* Stream       = nullptr;
};
