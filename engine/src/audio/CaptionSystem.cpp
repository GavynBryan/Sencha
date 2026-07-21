#include <audio/CaptionSystem.h>

#include <app/GameContexts.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/CaptionRuntime.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

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

float ClipDurationSeconds(
    const AudioClipCache* clips,
    AudioClipHandle handle)
{
    if (clips == nullptr)
        return 0.0f;

    const AudioClip* clip = clips->Get(handle);
    if (clip == nullptr || clip->SampleRate == 0
        || clip->ChannelCount == 0)
    {
        return 0.0f;
    }

    const std::size_t frames =
        clip->Samples.size() / clip->ChannelCount;
    return static_cast<float>(frames)
        / static_cast<float>(clip->SampleRate);
}
} // namespace

void CaptionSystem::Audio(AudioContext& ctx)
{
    Update(
        Captions,
        (AudioBackend != nullptr && AudioBackend->IsValid())
            ? AudioBackend
            : nullptr,
        ctx.Entities,
        ctx.Partitions,
        static_cast<float>(ctx.Presentation.DeltaSeconds));
}

void CaptionSystem::Update(
    CaptionRuntime* captions,
    const AudioService* audio,
    World& world,
    const StoragePartitionSet& partitions,
    float dtSeconds)
{
    if (captions == nullptr)
        return;

    if (world.IsRegistered<AudioCaptionComponent>())
    {
        AudioSourceRuntime* runtime =
            world.TryGetResource<AudioSourceRuntime>();
        const AudioClipCache* clips =
            runtime != nullptr ? runtime->Clips : nullptr;
        const bool hasSources =
            world.IsRegistered<AudioSourceComponent>();
        const bool noAudio = audio == nullptr;

        Query<Write<AudioCaptionComponent>> query(world);
        query.ForEachChunkIn(partitions, [&](auto& view)
        {
            auto captionComponents =
                view.template Write<AudioCaptionComponent>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
            {
                const EntityId entity = view.Entity(i);
                AudioCaptionComponent& caption =
                    captionComponents[i];
                const AudioSourceComponent* source = hasSources
                    ? world.TryGet<AudioSourceComponent>(entity)
                    : nullptr;

                if (source == nullptr)
                {
                    if (!caption.WarnedOrphan)
                    {
                        caption.WarnedOrphan = true;
                        captions->ContentLog().Warn(
                            "CaptionSystem: AudioCaptionComponent without an "
                            "AudioSourceComponent sibling is inert");
                    }
                    continue;
                }

                if (source->Voice.IsValid()
                    && source->Voice != caption.CaptionedVoice)
                {
                    caption.Caption = captions->BeginVoiceCaption(
                        source->Voice,
                        MakePayload(caption),
                        CaptionSource{ entity },
                        ClipDurationSeconds(clips, source->Clip));
                    caption.CaptionedVoice = source->Voice;
                    caption.CaptionAttempted = true;
                    continue;
                }

                const bool playbackFailed = source->Started
                    || (noAudio && source->PlayOnActive);
                if (!source->Voice.IsValid()
                    && playbackFailed
                    && !caption.CaptionAttempted)
                {
                    caption.CaptionAttempted = true;
                    caption.Caption = captions->BeginVoiceCaption(
                        {},
                        MakePayload(caption),
                        CaptionSource{ entity },
                        ClipDurationSeconds(clips, source->Clip));
                }
            }
        });
    }

    captions->Tick(audio, dtSeconds);
}
