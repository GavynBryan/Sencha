#include <audio/AudioVerbs.h>

#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceRuntime.h>
#include <authored/AuthoredValue.h>
#include <core/assets/AssetRef.h>
#include <ecs/World.h>

#include <algorithm>
#include <string>

namespace
{
constexpr std::string_view kDefaultBus = "Sfx";

VerbDefinition Cosmetic(std::string_view name, std::string displayName, std::string description, VerbKind kind)
{
    VerbDefinition verb;
    verb.Name = std::string(name);
    verb.DisplayName = std::move(displayName);
    verb.Description = std::move(description);
    verb.Category = "Audio";
    verb.Kind = kind;
    verb.Scope = AuthoredScope::Cosmetic;
    verb.Prediction = AuthoredPrediction::Presentation;
    DataFieldSchema clip = MakeDataField(DataFieldKind::AssetRef, "clip", "Clip", "The sound to play.");
    clip.Reference.AssetTypeFilter = AssetType::Audio;
    DataFieldSchema gain = MakeDataField(DataFieldKind::Float, "gain", "Gain", "Zero is silent, one is as recorded.");
    gain.Required = false;
    gain.Default = 1.0;
    gain.Numeric.Minimum = 0.0;
    gain.Numeric.Maximum = 1.0;
    DataFieldSchema bus = MakeDataField(DataFieldKind::String, "bus", "Bus", "The mixer bus it plays on.");
    bus.Required = false;
    bus.Default = std::string(kDefaultBus);
    verb.Arguments.Children = { std::move(clip), std::move(gain), std::move(bus) };
    return verb;
}
} // namespace

void DeclareAudioVerbs(VerbRegistrationScope& scope)
{
    (void)scope.Declare(Cosmetic(kAudioPlayVerb, "Play sound", "Plays a sound once.", VerbKind::Instant));
    (void)scope.Declare(Cosmetic(kAudioLoopLease, "Loop sound", "Plays a sound on a loop while held.", VerbKind::Lease));
}

AudioOperations::~AudioOperations()
{
    if (const AudioSourceRuntime* runtime = Entities->TryGetResource<AudioSourceRuntime>();
        runtime != nullptr && runtime->Audio != nullptr)
        for (const auto& [owner, voice] : Loops)
            runtime->Audio->Stop(voice);
}

VoiceId AudioOperations::Start(const VerbInvocation& invocation, bool looping, VerbAdmission& admission)
{
    const AudioSourceRuntime* runtime = Entities->TryGetResource<AudioSourceRuntime>();
    const AssetRef* clip = nullptr;
    double gain = 1.0;
    std::string_view bus = kDefaultBus;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetAsset(0, clip)
        || !invocation.Arguments->TryGetFloat(1, gain))
    {
        admission = VerbAdmission::InvalidArguments;
        return {};
    }
    (void)invocation.Arguments->TryGetString(2, bus);
    if (runtime == nullptr || runtime->Audio == nullptr || runtime->Clips == nullptr)
    {
        admission = VerbAdmission::Unavailable;
        return {};
    }
    const AudioClipHandle handle = runtime->Clips->Find(clip->Path);
    const AudioClip* loaded = runtime->Clips->Get(handle);
    if (loaded == nullptr)
    {
        admission = VerbAdmission::Refused;
        return {};
    }
    PlayParams params;
    params.Bus = bus;
    params.Gain = static_cast<float>(gain);
    params.Looping = looping;
    const VoiceId voice = runtime->Audio->Play(AudioClipKey{ SlotIndex(handle) }, *loaded, params);
    admission = voice.IsValid() ? VerbAdmission::Accepted : VerbAdmission::Refused;
    return voice;
}

VerbAdmission AudioOperations::Play(const VerbInvocation& invocation)
{
    VerbAdmission admission = VerbAdmission::Accepted;
    (void)Start(invocation, false, admission);
    return admission;
}

VerbAdmission AudioOperations::Loop(const VerbInvocation& invocation)
{
    Sweep();
    if (invocation.Step == LeaseStep::Release)
    {
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
    VerbAdmission admission = VerbAdmission::Accepted;
    const VoiceId voice = Start(invocation, true, admission);
    if (admission == VerbAdmission::Accepted)
        Loops.emplace_back(invocation.Owner, voice);
    return admission;
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
