#include <audio/AudioSystem.h>

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <core/identity/Id.h>
#include <core/service/ServiceHost.h>
#include <world/registry/Registry.h>

#include <unordered_set>

void AudioSystem::Audio(AudioContext& ctx)
{
    AudioService* audio = ctx.EngineInstance.Services().TryGet<AudioService>();
    Update(audio, ctx.ActiveRegistries);
}

void AudioSystem::Update(AudioService* audio, std::span<Registry*> active)
{
    if (audio == nullptr || !audio->IsValid())
        return;

    // Tick first: retire drained voices so their slots are reusable by the
    // plays this frame issues (Decision D).
    audio->Tick();

    std::unordered_set<Registry*> activeSet;
    activeSet.reserve(active.size());
    for (Registry* registry : active)
        if (registry != nullptr)
            activeSet.insert(registry);

    // Sweep: registries no longer in the audio view (dormant or detached)
    // get their voices stopped — by id only, never touching the registry.
    for (auto it = PlayingByRegistry.begin(); it != PlayingByRegistry.end();)
    {
        if (activeSet.contains(it->first))
        {
            ++it;
            continue;
        }

        for (VoiceId voice : it->second)
            audio->Stop(voice);
        it = PlayingByRegistry.erase(it);
    }

    // Visit: apply the start/stop rules and rebuild each active registry's
    // voice list.
    for (Registry* registry : active)
    {
        if (registry == nullptr)
            continue;

        std::vector<VoiceId>& playing = PlayingByRegistry[registry];
        playing.clear();
        DriveRegistry(*audio, *registry, playing);
    }
}

void AudioSystem::DriveRegistry(AudioService& audio, Registry& registry,
                                std::vector<VoiceId>& playing)
{
    if (!registry.Components.IsRegistered<AudioSourceComponent>())
        return;

    // The clip cache lives on the same per-registry resource the component
    // hooks use; without it we cannot resolve PCM to hand AudioService.
    auto* runtime = registry.Components.TryGetResource<AudioSourceRuntime>();
    AudioClipCache* clips = runtime ? runtime->Clips : nullptr;
    if (clips == nullptr)
        return;

    registry.Components.ForEachComponent<AudioSourceComponent>(
        [&](EntityId, AudioSourceComponent& source)
    {
        const bool live = audio.IsPlaying(source.Voice) || audio.IsPaused(source.Voice);

        if (!source.PlayOnActive)
        {
            // Inert data: stop any voice it was driving and stay silent until
            // gameplay flips it back on.
            if (live)
            {
                audio.Stop(source.Voice);
                source.Voice = {};
            }
            return;
        }

        if (source.Looping)
        {
            // Playing whenever its zone is in the audio view; a stolen or
            // retired voice (stale id resolves to not-live) restarts here.
            if (!live)
                source.Voice = {};
        }
        else
        {
            // One-shot: fires once per component lifetime. Zone re-entry does
            // not replay (Started latches).
            if (source.Started)
            {
                if (source.Voice.IsValid() && live)
                    playing.push_back(source.Voice);
                return;
            }
        }

        if (source.Voice.IsValid() && live)
        {
            // Already playing (looping, mid-flight): keep it and record it so
            // the sweep can stop it if the zone goes dormant.
            playing.push_back(source.Voice);
            return;
        }

        const AudioClip* clip = clips->Get(source.Clip);
        if (clip == nullptr)
            return;

        PlayParams params;
        params.Bus = source.Bus.View();
        params.Gain = source.Gain;
        params.Pan = source.Pan;
        params.Looping = source.Looping;

        source.Voice = audio.Play(AssetId{ source.Clip.Id }, *clip, params);
        source.Started = true;
        if (source.Voice.IsValid())
            playing.push_back(source.Voice);
    });
}
