#include <audio/AudioService.h>
#include "AudioVoice.h"   // AudioVoiceSlot -- private to src/audio/

#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <limits>

// ---------------------------------------------------------------------------
// Handle encoding
//
// VoiceId::Id packs a 20-bit slot index and a 12-bit generation counter into
// one uint32_t, matching the TextureHandle / ImageHandle convention used
// throughout the engine. Generation 0 is reserved for the null/invalid state.
// ---------------------------------------------------------------------------
namespace
{
    constexpr uint32_t kIndexBits     = 20u;
    constexpr uint32_t kIndexMask     = (1u << kIndexBits) - 1u;
    constexpr uint32_t kMaxGeneration = (1u << (32u - kIndexBits)) - 1u;
    constexpr uint32_t kInvalidSlot   = std::numeric_limits<uint32_t>::max();

    uint32_t DecodeIndex(uint32_t id)      { return id & kIndexMask; }
    uint32_t DecodeGeneration(uint32_t id) { return id >> kIndexBits; }
    uint32_t EncodeId(uint32_t index, uint32_t gen) { return (gen << kIndexBits) | (index & kIndexMask); }

    // All SDL streams are opened to this spec so the device sees uniform data.
    SDL_AudioSpec DeviceSpec()
    {
        SDL_AudioSpec s{};
        s.format   = SDL_AUDIO_S16;
        s.channels = 2;
        s.freq     = 44100;
        return s;
    }
} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AudioService::AudioService(LoggingProvider& logging, const EngineAudioConfig& config)
    : Log(logging.GetLogger<AudioService>())
{
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            Log.Error("AudioService: SDL audio init failed: {}", SDL_GetError());
            return;
        }
        OwnsAudioSubsystem = true;
    }

    const SDL_AudioSpec spec = DeviceSpec();
    DeviceId = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (DeviceId == 0)
    {
        Log.Error("AudioService: SDL_OpenAudioDevice failed: {}", SDL_GetError());
        return;
    }

    // Slot 0 is reserved so that VoiceId::Id == 0 always means invalid.
    Voices.emplace_back();

    // Built-in Engine bus -- hardcoded, always present.
    {
        BusEntry& e       = Buses.emplace_back();
        e.Bus.Name        = "Engine";
        e.Bus.MaxVoices   = 1;
        e.Bus.Volume      = 1.0f;
        e.Bus.Muted       = false;
        e.Bus.StealPolicy = VoiceStealPolicy::Reject;
    }

    for (const EngineAudioBusConfig& bc : config.Buses)
    {
        if (bc.Name.empty() || bc.MaxVoices == 0)
        {
            Log.Error("AudioService: skipping invalid bus config (empty name or MaxVoices == 0)");
            continue;
        }
        if (FindBus(bc.Name) != nullptr)
        {
            Log.Error("AudioService: duplicate bus name '{}', skipping", bc.Name);
            continue;
        }

        BusEntry& e       = Buses.emplace_back();
        e.Bus.Name        = bc.Name;
        e.Bus.MaxVoices   = bc.MaxVoices;
        e.Bus.Volume      = bc.Volume;
        e.Bus.Muted       = bc.Muted;
        e.Bus.StealPolicy = bc.StealPolicy;
    }

    Valid = true;
    Log.Info("AudioService: initialized ({} buses)", Buses.size());
}

AudioService::~AudioService()
{
    for (uint32_t i = 1; i < static_cast<uint32_t>(Voices.size()); ++i)
    {
        if (Voices[i].State != VoiceState::Idle)
            RetireVoice(i);
    }

    if (DeviceId != 0)
    {
        SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(DeviceId));
        DeviceId = 0;
    }

    if (OwnsAudioSubsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        Log.Info("AudioService: SDL audio subsystem shut down");
    }
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

VoiceId AudioService::Play(AssetId clipId, const AudioClip& clip, const PlayParams& params)
{
    if (!Valid)
        return {};

    if (!clip.IsValid())
    {
        Log.Error("AudioService::Play: invalid AudioClip for asset {}", clipId.Value);
        return {};
    }

    BusEntry* bus = FindBus(params.Bus);
    if (!bus)
    {
        Log.Error("AudioService::Play: unknown bus '{}'", params.Bus);
        return {};
    }

    const uint32_t slotIndex = AllocVoice(*bus);
    if (slotIndex == kInvalidSlot)
        return {};

    SDL_AudioSpec srcSpec{};
    srcSpec.format   = SDL_AUDIO_S16;
    srcSpec.channels = clip.ChannelCount;
    srcSpec.freq     = static_cast<int>(clip.SampleRate);

    const SDL_AudioSpec dstSpec = DeviceSpec();

    SDL_AudioStream* stream = SDL_CreateAudioStream(&srcSpec, &dstSpec);
    if (!stream)
    {
        Log.Error("AudioService::Play: SDL_CreateAudioStream failed: {}", SDL_GetError());
        RetireVoice(slotIndex);
        return {};
    }

    if (!SDL_BindAudioStream(static_cast<SDL_AudioDeviceID>(DeviceId), stream))
    {
        Log.Error("AudioService::Play: SDL_BindAudioStream failed: {}", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        RetireVoice(slotIndex);
        return {};
    }

    if (!SDL_PutAudioStreamData(stream, clip.Samples.data(), static_cast<int>(clip.ByteSize())))
    {
        Log.Error("AudioService::Play: SDL_PutAudioStreamData failed: {}", SDL_GetError());
        SDL_UnbindAudioStream(stream);
        SDL_DestroyAudioStream(stream);
        RetireVoice(slotIndex);
        return {};
    }

    if (!params.Looping)
        SDL_FlushAudioStream(stream);

    AudioVoiceSlot& slot = Voices[slotIndex];
    slot.ClipId        = clipId;
    slot.BusIndex      = static_cast<uint32_t>(bus - Buses.data());
    slot.State         = VoiceState::Playing;
    slot.Looping       = params.Looping;
    slot.Gain          = params.Gain;
    slot.Pan           = params.Pan;
    slot.FrameCursor   = 0;
    slot.StartTick     = Ticks;
    slot.Stream        = stream;

    ApplyVoiceGain(slot, bus->Bus);

    return MakeVoiceId(slotIndex, slot.Generation);
}

void AudioService::Stop(VoiceId id)
{
    if (Resolve(id))
        RetireVoice(VoiceIndex(id));
}

void AudioService::Pause(VoiceId id)
{
    AudioVoiceSlot* slot = Resolve(id);
    if (!slot || slot->State != VoiceState::Playing) return;

    SDL_PauseAudioStreamDevice(slot->Stream);
    slot->State = VoiceState::Paused;
}

void AudioService::Resume(VoiceId id)
{
    AudioVoiceSlot* slot = Resolve(id);
    if (!slot || slot->State != VoiceState::Paused) return;

    SDL_ResumeAudioStreamDevice(slot->Stream);
    slot->State = VoiceState::Playing;
}

// ---------------------------------------------------------------------------
// Bus controls
// ---------------------------------------------------------------------------

bool AudioService::SetBusVolume(std::string_view busName, float volume)
{
    BusEntry* bus = FindBus(busName);
    if (!bus) return false;

    bus->Bus.Volume = volume;
    for (uint32_t i : bus->VoiceIndices)
        ApplyVoiceGain(Voices[i], bus->Bus);

    return true;
}

bool AudioService::SetBusMuted(std::string_view busName, bool muted)
{
    BusEntry* bus = FindBus(busName);
    if (!bus) return false;

    bus->Bus.Muted = muted;
    for (uint32_t i : bus->VoiceIndices)
        ApplyVoiceGain(Voices[i], bus->Bus);

    return true;
}

float AudioService::GetBusVolume(std::string_view busName) const
{
    const BusEntry* bus = FindBus(busName);
    return bus ? bus->Bus.Volume : 0.0f;
}

bool AudioService::IsBusMuted(std::string_view busName) const
{
    const BusEntry* bus = FindBus(busName);
    return bus ? bus->Bus.Muted : false;
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void AudioService::Tick()
{
    if (!Valid) return;

    ++Ticks;

    for (uint32_t i = 1; i < static_cast<uint32_t>(Voices.size()); ++i)
    {
        AudioVoiceSlot& slot = Voices[i];
        if (slot.State != VoiceState::Playing) continue;

        const int queued = SDL_GetAudioStreamQueued(slot.Stream);

        if (slot.Looping)
        {
            // Looping voices hold an open stream that the caller is expected to
            // keep fed. Sencha v1 does not hold a back-pointer to the AudioClip,
            // so refilling is the caller's responsibility via Stop/Play cycling
            // or a future AudioClipCache integration. Nothing to do here yet.
            continue;
        }

        if (queued == 0)
            RetireVoice(i);
    }
}

// ---------------------------------------------------------------------------
// Voice state queries
// ---------------------------------------------------------------------------

bool AudioService::IsPlaying(VoiceId id) const
{
    const AudioVoiceSlot* slot = Resolve(id);
    return slot && slot->State == VoiceState::Playing;
}

bool AudioService::IsPaused(VoiceId id) const
{
    const AudioVoiceSlot* slot = Resolve(id);
    return slot && slot->State == VoiceState::Paused;
}

VoiceState AudioService::GetState(VoiceId id) const
{
    const AudioVoiceSlot* slot = Resolve(id);
    return slot ? slot->State : VoiceState::Idle;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

AudioService::BusEntry* AudioService::FindBus(std::string_view name)
{
    for (BusEntry& b : Buses)
    {
        if (b.Bus.Name == name) return &b;
    }
    return nullptr;
}

const AudioService::BusEntry* AudioService::FindBus(std::string_view name) const
{
    for (const BusEntry& b : Buses)
    {
        if (b.Bus.Name == name) return &b;
    }
    return nullptr;
}

uint32_t AudioService::AllocVoice(BusEntry& busEntry)
{
    const uint32_t capacity = busEntry.Bus.MaxVoices;
    const uint32_t inUse    = static_cast<uint32_t>(busEntry.VoiceIndices.size());

    if (inUse < capacity)
    {
        // Bus has room. Try to reuse a free slot before growing the pool.
        if (!FreeVoiceSlots.empty())
        {
            const uint32_t slot = FreeVoiceSlots.back();
            FreeVoiceSlots.pop_back();
            busEntry.VoiceIndices.push_back(slot);
            return slot;
        }

        // No free slots -- grow the pool.
        const uint32_t slot = static_cast<uint32_t>(Voices.size());
        AudioVoiceSlot& s   = Voices.emplace_back();
        s.Generation = 1u;
        busEntry.VoiceIndices.push_back(slot);
        return slot;
    }

    // Bus is full.
    if (busEntry.Bus.StealPolicy == VoiceStealPolicy::StealOldest)
    {
        uint32_t oldestSlot = kInvalidSlot;
        uint32_t oldestTick = std::numeric_limits<uint32_t>::max();

        for (uint32_t idx : busEntry.VoiceIndices)
        {
            if (Voices[idx].StartTick < oldestTick)
            {
                oldestTick = Voices[idx].StartTick;
                oldestSlot = idx;
            }
        }

        assert(oldestSlot != kInvalidSlot);
        RetireVoice(oldestSlot);

        // RetireVoice removed oldestSlot from VoiceIndices; reclaim it for the new voice.
        busEntry.VoiceIndices.push_back(oldestSlot);
        return oldestSlot;
    }

    return kInvalidSlot;
}

void AudioService::RetireVoice(uint32_t slotIndex)
{
    assert(slotIndex > 0 && slotIndex < Voices.size());

    AudioVoiceSlot& slot = Voices[slotIndex];

    if (slot.Stream)
    {
        SDL_UnbindAudioStream(slot.Stream);
        SDL_DestroyAudioStream(slot.Stream);
        slot.Stream = nullptr;
    }

    if (slot.State != VoiceState::Idle)
    {
        auto& indices = Buses[slot.BusIndex].VoiceIndices;
        indices.erase(std::remove(indices.begin(), indices.end(), slotIndex), indices.end());
    }

    // Bump generation so outstanding VoiceIds for this slot become stale.
    uint32_t gen = slot.Generation + 1u;
    if (gen == 0u || gen > kMaxGeneration) gen = 1u;

    slot            = AudioVoiceSlot{};
    slot.Generation = gen;
    // State defaults to Idle via the default constructor.

    FreeVoiceSlots.push_back(slotIndex);
}

void AudioService::ApplyVoiceGain(const AudioVoiceSlot& slot, const AudioBus& bus) const
{
    if (!slot.Stream) return;
    const float effective = bus.Muted ? 0.0f : (slot.Gain * bus.Volume);
    SDL_SetAudioStreamGain(slot.Stream, effective);
}

AudioVoiceSlot* AudioService::Resolve(VoiceId id)
{
    if (!id.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(id.Id);
    const uint32_t gen   = DecodeGeneration(id.Id);
    if (index == 0 || index >= Voices.size()) return nullptr;
    AudioVoiceSlot& slot = Voices[index];
    if (slot.Generation != gen || slot.State == VoiceState::Idle) return nullptr;
    return &slot;
}

const AudioVoiceSlot* AudioService::Resolve(VoiceId id) const
{
    if (!id.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(id.Id);
    const uint32_t gen   = DecodeGeneration(id.Id);
    if (index == 0 || index >= Voices.size()) return nullptr;
    const AudioVoiceSlot& slot = Voices[index];
    if (slot.Generation != gen || slot.State == VoiceState::Idle) return nullptr;
    return &slot;
}

VoiceId AudioService::MakeVoiceId(uint32_t index, uint32_t generation)
{
    VoiceId id;
    id.Id = EncodeId(index, generation);
    return id;
}

uint32_t AudioService::VoiceIndex(VoiceId id)
{
    return DecodeIndex(id.Id);
}
