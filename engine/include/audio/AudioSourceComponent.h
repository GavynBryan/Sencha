#pragma once

#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioVoice.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/text/InlineString.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

#include <string_view>
#include <tuple>
#include <type_traits>

// Bus names are short, config-defined, and live inside an ECS component, so
// they are a fixed-capacity inline string (the archetype storage relocates
// components with memcpy — see InlineString).
using BusName = InlineString<32>;

//=============================================================================
// AudioSourceComponent (docs/audio/runtime.md, Decision B)
//
// A scene-resident audio emitter: ambient loops and placed one-shots,
// authored in scene JSON and streamed with the zone. Imperative
// fire-and-forget SFX still calls AudioService::Play directly; streamed
// music is a separate later slice. This is the emitter, not a player —
// the imperative control surface is AudioService.
//
// Authored fields are serialized through TypeSchema; the runtime fields
// (Voice, Started) are not — they default-initialize on load and are
// driven by AudioSystem.
//=============================================================================
struct AudioSourceComponent
{
    // -- Authored (serialized) -----------------------------------------------
    AudioClipHandle Clip;
    BusName Bus = "Sfx";
    float Gain = 1.0f;          // [0, 1]
    float Pan = 0.0f;           // [-1 left, +1 right], static (no listener yet)
    bool Looping = false;
    bool PlayOnActive = true;   // emit while the zone is audio-active

    // -- Runtime (not serialized) --------------------------------------------
    // The voice this source currently drives. Generational, so a stale id
    // (the voice was stolen or retired) resolves to nothing and AudioSystem
    // treats the source as not playing.
    VoiceId Voice;
    // One-shots fire once per component lifetime; loops use Voice validity.
    bool Started = false;
};

class CaptionRuntime;

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

// Archetype storage relocates components with memcpy, so the component must
// stay trivially copyable (enforced by World::RegisterComponent) — this is
// why Bus is an InlineString, not std::string.

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
            MakeField("clip", &AudioSourceComponent::Clip),
            MakeField("bus", &AudioSourceComponent::Bus).Default(BusName("Sfx")),
            MakeField("gain", &AudioSourceComponent::Gain).Default(1.0f),
            MakeField("pan", &AudioSourceComponent::Pan).Default(0.0f),
            MakeField("looping", &AudioSourceComponent::Looping).Default(false),
            MakeField("play_on_active", &AudioSourceComponent::PlayOnActive).Default(true),
        };
    }
};
