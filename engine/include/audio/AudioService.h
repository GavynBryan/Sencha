#pragma once

#include <audio/AudioBus.h>
#include <audio/AudioVoice.h>
#include <assets/audio/AudioClip.h>
#include <core/identity/Id.h>
#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Private implementation struct defined in src/audio/AudioVoice.h.
// Never exposed through the public API; callers hold VoiceIds only.
struct AudioVoiceSlot;

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
// Top-level config passed to AudioService at construction time. Buses defined
// here are created after (and in addition to) the built-in Engine bus.
//=============================================================================
struct EngineAudioConfig
{
    std::vector<EngineAudioBusConfig> Buses;
};

//=============================================================================
// PlayParams
//
// Optional per-play overrides. Default-constructed plays the clip on the
// Engine bus at full gain, centered, non-looping.
//=============================================================================
struct PlayParams
{
    std::string_view Bus     = "Engine";
    float            Gain    = 1.0f;   // [0, 1]
    float            Pan     = 0.0f;   // [-1 left, 0 center, +1 right]
    bool             Looping = false;
};

//=============================================================================
// AudioService
//
// Main entry point for audio playback and audio system lifecycle.
//
// Responsibilities:
//   - Initialize and shut down the SDL audio backend
//   - Build the bus table from EngineAudioConfig (plus the built-in Engine bus)
//   - Allocate voices from buses using the bus's VoiceStealPolicy
//   - Handle Play / Stop / Pause / Resume
//   - Expose per-bus Volume and Mute controls
//   - Tick active voices each frame (retire completed non-looping voices)
//
// Ownership:
//   - AudioService owns all voice slots and their SDL_AudioStreams.
//   - AudioService does not own AudioClips. Play() receives a const reference;
//     the caller (or asset cache) is responsible for keeping the clip alive
//     for the duration of playback.
//   - Callers receive VoiceIds and must not hold raw slot pointers.
//
// Voice allocation policy (per bus, set in EngineAudioBusConfig):
//   Reject      -- returns an invalid VoiceId when the bus is full.
//   StealOldest -- silently recycles the voice that started playing earliest.
//
// Built-in buses:
//   "Engine" -- MaxVoices: 1, Reject policy. Reserved for engine-level
//   sounds. Always present; do not include it in EngineAudioConfig.
//=============================================================================
class AudioService : public IService
{
public:
    AudioService(LoggingProvider& logging, const EngineAudioConfig& config);
    ~AudioService() override;

    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;
    AudioService(AudioService&&) = delete;
    AudioService& operator=(AudioService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // -- Playback -------------------------------------------------------------

    // Begin playback of `clip` (identified by `clipId` for diagnostics) on the
    // bus named in `params.Bus`. Returns a valid VoiceId on success or an
    // invalid VoiceId if the bus is full and its policy is Reject, the clip is
    // invalid, or the bus name is not found.
    //
    // `clip` must remain valid for the duration of playback. For looping clips
    // the caller must ensure the clip is not freed while the voice is active.
    [[nodiscard]] VoiceId Play(AssetId clipId, const AudioClip& clip,
                               const PlayParams& params = {});

    // Stop the voice immediately and return its slot to the pool. Calling
    // Stop on an invalid or already-stopped VoiceId is a no-op.
    void Stop(VoiceId voice);

    // Pause the voice. No-op if the voice is not currently Playing.
    void Pause(VoiceId voice);

    // Resume a paused voice. No-op if the voice is not currently Paused.
    void Resume(VoiceId voice);

    // -- Bus controls ---------------------------------------------------------

    // Returns false if `busName` does not exist.
    bool SetBusVolume(std::string_view busName, float volume);
    bool SetBusMuted(std::string_view busName, bool muted);

    [[nodiscard]] float GetBusVolume(std::string_view busName) const;
    [[nodiscard]] bool  IsBusMuted(std::string_view busName)   const;

    // -- Per-frame update -----------------------------------------------------

    // Retire non-looping voices whose SDL streams have fully drained. Should
    // be called once per frame from the audio system or main loop.
    void Tick();

    // -- Voice state queries --------------------------------------------------

    [[nodiscard]] bool       IsPlaying(VoiceId voice) const;
    [[nodiscard]] bool       IsPaused(VoiceId voice)  const;
    [[nodiscard]] VoiceState GetState(VoiceId voice)  const;

private:
    struct BusEntry
    {
        AudioBus              Bus;
        std::vector<uint32_t> VoiceIndices; // slot indices into Voices
    };

    Logger&   Log;
    uint32_t  DeviceId           = 0; // SDL_AudioDeviceID (uint32_t)
    bool      Valid              = false;
    bool      OwnsAudioSubsystem = false;
    uint32_t  Ticks              = 0; // monotonic counter for StealOldest

    std::vector<BusEntry>      Buses;
    std::vector<AudioVoiceSlot> Voices;
    std::vector<uint32_t>      FreeVoiceSlots;

    [[nodiscard]] BusEntry*       FindBus(std::string_view name);
    [[nodiscard]] const BusEntry* FindBus(std::string_view name) const;

    [[nodiscard]] uint32_t AllocVoice(BusEntry& busEntry);
    void RetireVoice(uint32_t slotIndex);
    void ApplyVoiceGain(const AudioVoiceSlot& slot, const AudioBus& bus) const;

    [[nodiscard]] AudioVoiceSlot*       Resolve(VoiceId voice);
    [[nodiscard]] const AudioVoiceSlot* Resolve(VoiceId voice) const;

    [[nodiscard]] static VoiceId  MakeVoiceId(uint32_t index, uint32_t generation);
    [[nodiscard]] static uint32_t VoiceIndex(VoiceId voice);
};
