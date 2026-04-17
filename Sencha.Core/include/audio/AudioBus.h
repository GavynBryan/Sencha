#pragma once

#include <cstdint>
#include <string>

//=============================================================================
// VoiceStealPolicy
//
// Determines what AudioService does when a bus is full and a new voice is
// requested. StealOldest replaces the voice that started playing earliest.
// Reject silently drops the request and returns an invalid VoiceId.
//=============================================================================
enum class VoiceStealPolicy : uint8_t
{
    Reject,
    StealOldest,
};

//=============================================================================
// AudioBus
//
// A named pool of voices with a shared volume and mute state. Buses are
// configured at startup (via EngineAudioConfig) and remain fixed for the
// lifetime of AudioService. Game code refers to buses by name through
// AudioService; it does not hold AudioBus pointers directly.
//
// MaxVoices == 0 is invalid. A bus with MaxVoices == 1 is appropriate for
// music and dialogue where only one track should play at a time.
//=============================================================================
struct AudioBus
{
    std::string      Name;
    uint8_t          MaxVoices   = 0;
    float            Volume      = 1.0f;
    bool             Muted       = false;
    VoiceStealPolicy StealPolicy = VoiceStealPolicy::Reject;
};
