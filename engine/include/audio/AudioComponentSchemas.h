#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/Caption.h>
#include <audio/CaptionRuntime.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape, clip lifetime, and voice teardown for the audio components.
//
// Registration and the serializers include this. A system that reads one of
// these components includes the component and gets its values, not the
// services that own what the values refer to.
//=============================================================================

//=============================================================================
// AudioSourceRuntime
//
// World resource the audio component hooks reach through —
// AudioSourceComponent for clips/voices and AudioCaptionComponent for
// captions. Any pointer may be null in headless worlds (no audio device,
// tests, captions unused). The pointers are plain data, safe to store
// off-thread during an async zone build; only the main thread dereferences
// them.
//=============================================================================
struct AudioSourceRuntime
{
    AudioSourceRuntime() = default;
    AudioSourceRuntime(AudioClipCache* clips, AudioService* audio,
                       CaptionRuntime* captions = nullptr)
        : Clips(clips)
        , Audio(audio)
        , Captions(captions)
    {
    }

    AudioClipCache* Clips = nullptr;
    AudioService* Audio = nullptr;
    CaptionRuntime* Captions = nullptr;
};

template <>
struct ComponentTraits<AudioSourceComponent>
{
    // OnAdd retains the clip and nothing more: deserialization is not
    // activation, and the zone may be dormant. AudioSystem starts playback.
    static void OnAdd(AudioSourceComponent& component, World& world, EntityId)
    {
        auto* runtime = world.TryGetResource<AudioSourceRuntime>();
        if (runtime == nullptr || runtime->Clips == nullptr)
            return;

        runtime->Clips->Retain(component.Clip);
    }

    // OnRemove enforces the slice's one invariant (docs/audio/runtime.md,
    // Decision C): a voice never outlives the clip reference that feeds it.
    // Stop first, then release — in that order, in this hook, which fires on
    // both entity destruction and zone detach.
    static void OnRemove(const AudioSourceComponent& component, World& world, EntityId)
    {
        auto* runtime = world.TryGetResource<AudioSourceRuntime>();
        if (runtime == nullptr)
            return;

        if (runtime->Audio != nullptr)
            runtime->Audio->Stop(component.Voice);
        if (runtime->Clips != nullptr)
            runtime->Clips->Release(component.Clip);
    }
};

template <>
struct TypeSchema<AudioSourceComponent>
{
    static constexpr std::string_view Name = "AudioSource";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('A', 'S', 'R', 'C');

    static auto Fields()
    {
        return std::tuple{
            MakeField("clip", &AudioSourceComponent::Clip).AsAsset(AssetType::Audio),
            MakeField("bus", &AudioSourceComponent::Bus).Default(BusName("Sfx")),
            MakeField("gain", &AudioSourceComponent::Gain).Default(1.0f),
            MakeField("pan", &AudioSourceComponent::Pan).Default(0.0f),
            MakeField("looping", &AudioSourceComponent::Looping).Default(false),
            MakeField("play_on_active", &AudioSourceComponent::PlayOnActive).Default(true),
        };
    }
};

template <>
struct ComponentTraits<AudioCaptionComponent>
{
    // No OnAdd: there is no asset edge to retain, and deserialization is not
    // activation — CaptionSystem begins captions.

    // OnRemove ends the active caption (entity destruction and zone detach
    // both fire it). Stale-safe: an already-retired caption is a no-op, and
    // ordering against the sibling source's own OnRemove does not matter.
    static void OnRemove(const AudioCaptionComponent& component, World& world, EntityId)
    {
        auto* runtime = world.TryGetResource<AudioSourceRuntime>();
        if (runtime == nullptr || runtime->Captions == nullptr)
            return;

        runtime->Captions->EndCaption(component.Caption);
    }
};

template <>
struct TypeSchema<AudioCaptionComponent>
{
    static constexpr std::string_view Name = "AudioCaption";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('A', 'C', 'A', 'P');

    static auto Fields()
    {
        return std::tuple{
            MakeField("kind", &AudioCaptionComponent::Kind)
                .Default(CaptionKind::ClosedCaption),
            MakeField("channel", &AudioCaptionComponent::Channel)
                .Default(CaptionChannelName("World")),
            MakeField("priority", &AudioCaptionComponent::Priority)
                .Default(CaptionPriority::Gameplay),
            MakeField("text", &AudioCaptionComponent::Text),
            MakeField("speaker", &AudioCaptionComponent::Speaker)
                .Default(SpeakerKey()),
            MakeField("duration_seconds", &AudioCaptionComponent::DurationSeconds)
                .Default(0.0f),
            MakeField("merge_duplicates", &AudioCaptionComponent::MergeDuplicates)
                .Default(true),
        };
    }
};
