#pragma once

#include <audio/AudioVoice.h>
#include <ecs/StoragePartitionId.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class AudioService;
class StoragePartitionSet;
class World;
struct AudioContext;

// Drives AudioSourceComponent playback for the unified runtime World. Voice
// ownership is indexed by storage partition solely so zones leaving the Audio
// domain can be silenced without touching dormant entity rows.
class AudioSystem
{
public:
    explicit AudioSystem(AudioService* audio = nullptr)
        : AudioBackend(audio)
    {
    }

    void Audio(AudioContext& ctx);
    void Update(
        AudioService* audio,
        World& world,
        const StoragePartitionSet& partitions);

private:
    AudioService* AudioBackend = nullptr;
    std::unordered_map<std::uint16_t, std::vector<VoiceId>>
        PlayingByPartition;
};
