#include <audio/CaptionSystem.h>

#include <app/GameContexts.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/CaptionRuntime.h>
#include <world/registry/Registry.h>

namespace
{
    CaptionPayload MakePayload(const AudioCaptionComponent& caption)
    {
        return CaptionPayload{
            .Kind = caption.Kind,
            .Channel = caption.Channel,
            .Priority = caption.Priority,
            .Text = caption.Text,
            .Speaker = caption.Speaker,
            .DurationSeconds = caption.DurationSeconds,
            .MergeDuplicates = caption.MergeDuplicates,
        };
    }

    // Clip length for the Decision C degrade path's duration hint. Zero when
    // the clip is unavailable — CaptionRuntime then falls back to its
    // reading-speed estimate.
    float ClipDurationSeconds(const AudioClipCache* clips, AudioClipHandle handle)
    {
        if (clips == nullptr)
            return 0.0f;

        const AudioClip* clip = clips->Get(handle);
        if (clip == nullptr || clip->SampleRate == 0 || clip->ChannelCount == 0)
            return 0.0f;

        const std::size_t frames = clip->Samples.size() / clip->ChannelCount;
        return static_cast<float>(frames) / static_cast<float>(clip->SampleRate);
    }
}

void CaptionSystem::Audio(AudioContext& ctx)
{
    Update(Captions,
           (AudioBackend != nullptr && AudioBackend->IsValid()) ? AudioBackend : nullptr,
           ctx.ActiveRegistries,
           static_cast<float>(ctx.Presentation.DeltaSeconds));
}

void CaptionSystem::Update(CaptionRuntime* captions, const AudioService* audio,
                           std::span<Registry*> active, float dtSeconds)
{
    if (captions == nullptr)
        return;

    // Begin first, then tick: AudioSystem already ran this frame, so a voice
    // it started is captioned the same frame, and a loop-restart's merged
    // caption is rebound to the fresh voice before retirement would see the
    // dead one.
    for (Registry* registry : active)
        if (registry != nullptr)
            DriveRegistry(*captions, *registry, audio == nullptr);

    captions->Tick(audio, dtSeconds);
}

void CaptionSystem::DriveRegistry(CaptionRuntime& captions, Registry& registry,
                                  bool noAudio)
{
    if (!registry.Components.IsRegistered<AudioCaptionComponent>())
        return;

    auto* runtime = registry.Components.TryGetResource<AudioSourceRuntime>();
    const AudioClipCache* clips = runtime ? runtime->Clips : nullptr;
    const bool hasSources = registry.Components.IsRegistered<AudioSourceComponent>();

    registry.Components.ForEachComponent<AudioCaptionComponent>(
        [&](EntityId entity, AudioCaptionComponent& caption)
    {
        const AudioSourceComponent* source = hasSources
            ? registry.Components.TryGet<AudioSourceComponent>(entity)
            : nullptr;
        if (source == nullptr)
        {
            // Inert without a sibling source. A future text-only timed
            // caption component is the recorded alternative, deferred until
            // content asks (imperative BeginTimedCaption covers it today).
            if (!caption.WarnedOrphan)
            {
                caption.WarnedOrphan = true;
                captions.ContentLog().Warn(
                    "CaptionSystem: AudioCaptionComponent without an "
                    "AudioSourceComponent sibling is inert");
            }
            return;
        }

        // A fresh voice (first start, loop restart after dormancy, or a
        // late start after an earlier reject) gets a voice-bound caption.
        // A one-shot's latched stale voice equals CaptionedVoice and never
        // re-captions; a caption that hit its authored duration cap while
        // the loop kept playing stays ended for the same reason.
        if (source->Voice.IsValid() && source->Voice != caption.CaptionedVoice)
        {
            caption.Caption = captions.BeginVoiceCaption(
                source->Voice, MakePayload(caption), CaptionSource{ entity },
                ClipDurationSeconds(clips, source->Clip));
            caption.CaptionedVoice = source->Voice;
            caption.CaptionAttempted = true;
            return;
        }

        // Decision C degrade path, once per component lifetime: playback
        // failed and no voice ever began. Two shapes of failure land here —
        // the source attempted a play (Started latched) and was rejected,
        // or there is no usable audio service at all, in which case
        // AudioSystem never runs and Started never latches but an active
        // PlayOnActive source has still "failed to sound". Subtitles become
        // timed captions; closed captions stay silent (no sound happened to
        // describe). BeginVoiceCaption applies that split.
        const bool playbackFailed = source->Started
            || (noAudio && source->PlayOnActive);
        if (!source->Voice.IsValid() && playbackFailed && !caption.CaptionAttempted)
        {
            caption.CaptionAttempted = true;
            caption.Caption = captions.BeginVoiceCaption(
                {}, MakePayload(caption), CaptionSource{ entity },
                ClipDurationSeconds(clips, source->Clip));
        }
    });
}
