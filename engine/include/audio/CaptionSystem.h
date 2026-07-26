#pragma once

class AudioService;
class CaptionRuntime;
class StoragePartitionSet;
class World;
struct AudioContext;

// Starts captions for AudioCaptionComponent rows in the current Audio-domain
// partition set, then advances the simulation-wide CaptionRuntime once. Dormant
// partitions are never visited; voice-bound retirement remains centralized in
// CaptionRuntime and therefore needs no per-zone caption backend.
class CaptionSystem
{
public:
    CaptionSystem(
        CaptionRuntime* captions = nullptr,
        AudioService* audio = nullptr)
        : Captions(captions)
        , AudioBackend(audio)
    {
    }

    void Audio(AudioContext& ctx);
    void Update(
        CaptionRuntime* captions,
        const AudioService* audio,
        World& world,
        const StoragePartitionSet& partitions,
        float dtSeconds);

private:
    CaptionRuntime* Captions = nullptr;
    AudioService* AudioBackend = nullptr;
};
