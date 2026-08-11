#pragma once

#include <audio/AudioBus.h>
#include <core/json/JsonValue.h>

#include <optional>
#include <string>
#include <vector>

//=============================================================================
// EngineAudioBusConfig
//
// Load-time description of one bus. Deserialized from JSON at startup and
// compiled into a live AudioBus. The built-in Engine bus is hardcoded by
// AudioService and must not appear in this list.
//=============================================================================
struct EngineAudioBusConfig
{
    std::string      Name;
    uint8_t          MaxVoices   = 1;
    float            Volume      = 1.0f;
    bool             Muted       = false;
    VoiceStealPolicy StealPolicy = VoiceStealPolicy::Reject;
};

//=============================================================================
// EngineAudioConfig
//
// Top-level audio config passed to AudioService at construction time. Buses
// defined here are created after (and in addition to) the built-in Engine bus.
//=============================================================================
struct EngineAudioConfig
{
    std::vector<EngineAudioBusConfig> Buses;

    // Whether to open a playback device. A process nobody is listening to --
    // a dedicated host -- has no reason to hold one, and asking for one on a
    // machine with no sound card reports a failure that was never a problem.
    // The buses are still configured either way, so a malformed audio section
    // is still an error wherever it is read.
    bool EnablePlayback = true;
};

//=============================================================================
// AudioConfigError
//=============================================================================
struct AudioConfigError
{
    std::string Message;
};

// Deserialize EngineAudioConfig from the "audio" section of the engine config
// JSON object (i.e. pass root.Find("audio") here, or the object itself if
// the audio section is the root).
std::optional<EngineAudioConfig> DeserializeAudioConfig(
    const JsonValue& root,
    AudioConfigError* error = nullptr);
