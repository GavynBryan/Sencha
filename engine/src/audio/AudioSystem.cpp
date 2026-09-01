#include <audio/AudioSystem.h>
#include <audio/AudioComponentSchemas.h>

#include <app/GameContexts.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <core/identity/Id.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <unordered_set>

void AudioSystem::Audio(AudioContext& ctx)
{
    Update(AudioBackend, ctx.Entities, ctx.Partitions);
}

void AudioSystem::Update(
    AudioService* audio,
    World& world,
    const StoragePartitionSet& partitions)
{
    if (audio == nullptr || !audio->IsValid())
        return;

    // Drains and re-queues backend buffers. Deliberately per rendered frame
    // rather than per simulation tick: it is idempotent and only needs to run
    // often enough to keep the mixer fed, so pacing it off a second
    // accumulator would buy nothing.
    audio->Tick();

    std::unordered_set<std::uint16_t> active;
    active.reserve(partitions.Size());
    for (StoragePartitionId partition : partitions.Members())
        active.insert(partition.Value);

    for (auto it = PlayingByPartition.begin();
         it != PlayingByPartition.end();)
    {
        if (active.contains(it->first))
        {
            ++it;
            continue;
        }

        for (VoiceId voice : it->second)
            audio->Stop(voice);
        it = PlayingByPartition.erase(it);
    }

    if (!world.IsRegistered<AudioSourceComponent>())
        return;

    AudioSourceRuntime* runtime =
        world.TryGetResource<AudioSourceRuntime>();
    AudioClipCache* clips = runtime != nullptr ? runtime->Clips : nullptr;
    if (clips == nullptr)
        return;

    for (StoragePartitionId partition : partitions.Members())
        PlayingByPartition[partition.Value].clear();

    Query<Write<AudioSourceComponent>> query(world);
    query.ForEachChunkIn(partitions, [&](auto& view)
    {
        std::vector<VoiceId>& playing =
            PlayingByPartition[view.Partition().Value];
        auto sources = view.template Write<AudioSourceComponent>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            AudioSourceComponent& source = sources[i];
            const bool live =
                audio->IsPlaying(source.Voice)
                || audio->IsPaused(source.Voice);

            if (!source.PlayOnActive)
            {
                if (live)
                {
                    audio->Stop(source.Voice);
                    source.Voice = {};
                }
                continue;
            }

            if (source.Looping)
            {
                if (!live)
                    source.Voice = {};
            }
            else if (source.Started)
            {
                if (source.Voice.IsValid() && live)
                    playing.push_back(source.Voice);
                continue;
            }

            if (source.Voice.IsValid() && live)
            {
                playing.push_back(source.Voice);
                continue;
            }

            const AudioClip* clip = clips->Get(source.Clip);
            if (clip == nullptr)
                continue;

            PlayParams params;
            params.Bus = source.Bus.View();
            params.Gain = source.Gain;
            params.Pan = source.Pan;
            params.Looping = source.Looping;

            source.Voice = audio->Play(
                AudioClipKey{ SlotIndex(source.Clip) },
                *clip,
                params);
            source.Started = true;
            if (source.Voice.IsValid())
                playing.push_back(source.Voice);
        }
    });
}
