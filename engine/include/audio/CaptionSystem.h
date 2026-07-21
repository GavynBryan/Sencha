#pragma once

class AudioService;
class CaptionRuntime;
class StoragePartitionSet;
class World;
struct AudioContext;

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
