#include <audio/AudioVerbs.h>

#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceRuntime.h>
#include <ecs/World.h>

#include <algorithm>

AudioOperations::~AudioOperations()
{
    if (const AudioSourceRuntime* runtime = Entities->TryGetResource<AudioSourceRuntime>();
        runtime != nullptr && runtime->Audio != nullptr)
        for (const auto& [owner, voice] : Loops)
            runtime->Audio->Stop(voice);
}

VoiceId AudioOperations::Start(const AssetRef& clip, float gain, std::string_view bus, bool looping,
                               VerbAdmission& admission)
{
    const AudioSourceRuntime* runtime = Entities->TryGetResource<AudioSourceRuntime>();
    if (runtime == nullptr || runtime->Audio == nullptr || runtime->Clips == nullptr)
    {
        admission = VerbAdmission::Unavailable;
        return {};
    }
    const AudioClipHandle handle = runtime->Clips->Find(clip.Path);
    const AudioClip* loaded = runtime->Clips->Get(handle);
    if (loaded == nullptr)
    {
        admission = VerbAdmission::Refused;
        return {};
    }
    PlayParams params;
    params.Bus = bus;
    params.Gain = gain;
    params.Looping = looping;
    const VoiceId voice = runtime->Audio->Play(AudioClipKey{ SlotIndex(handle) }, *loaded, params);
    admission = voice.IsValid() ? VerbAdmission::Accepted : VerbAdmission::Refused;
    return voice;
}

VerbAdmission AudioOperations::Play(const AssetRef& clip, float gain, std::string_view bus)
{
    VerbAdmission admission = VerbAdmission::Accepted;
    (void)Start(clip, gain, bus, false, admission);
    return admission;
}

VerbAdmission AudioOperations::Loop(const VerbInvocation& invocation, const AssetRef& clip, float gain,
                                    std::string_view bus)
{
    Sweep();
    VerbAdmission admission = VerbAdmission::Accepted;
    const VoiceId voice = Start(clip, gain, bus, true, admission);
    if (admission == VerbAdmission::Accepted)
        Loops.emplace_back(invocation.Owner, voice);
    return admission;
}

VerbAdmission AudioOperations::EndLoop(const VerbInvocation& invocation)
{
    Sweep();
    const AudioSourceRuntime* runtime = Entities->TryGetResource<AudioSourceRuntime>();
    std::erase_if(Loops, [&](const std::pair<LeaseOwner, VoiceId>& loop) {
        if (loop.first != invocation.Owner)
            return false;
        if (runtime != nullptr && runtime->Audio != nullptr)
            runtime->Audio->Stop(loop.second);
        return true;
    });
    return VerbAdmission::Accepted;
}

void AudioOperations::Sweep()
{
    const AudioSourceRuntime* runtime = Entities->TryGetResource<AudioSourceRuntime>();
    std::erase_if(Loops, [&](const std::pair<LeaseOwner, VoiceId>& loop) {
        if (Entities->IsAlive(loop.first.Entity))
            return false;
        if (runtime != nullptr && runtime->Audio != nullptr)
            runtime->Audio->Stop(loop.second);
        return true;
    });
}
