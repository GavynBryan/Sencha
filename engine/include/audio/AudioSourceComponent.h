#pragma once

#include <audio/AudioVoice.h>
#include <audio/AudioClipHandle.h>
#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/ComponentAssetOwnership.h>

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
// The authored fields are the schema; the runtime fields (Voice, Started)
// default-initialize on load and are driven by AudioSystem.
//=============================================================================
struct SENCHA_COMPONENT("AudioSource")
       SENCHA_SCHEMA("AudioSource")
       SENCHA_SCENE_CHUNK("ASRC")
AudioSourceComponent
{
    // -- Authored (serialized) -----------------------------------------------
    SENCHA_FIELD("clip")
    SENCHA_ASSET(Audio)
    AudioClipHandle Clip;

    SENCHA_FIELD("bus")
    BusName Bus = "Sfx";

    SENCHA_FIELD("gain")
    float Gain = 1.0f;          // [0, 1]

    SENCHA_FIELD("pan")
    float Pan = 0.0f;           // [-1 left, +1 right], static (no listener yet)

    SENCHA_FIELD("looping")
    bool Looping = false;

    SENCHA_FIELD("play_on_active")
    bool PlayOnActive = true;   // emit while the zone is audio-active

    // -- Runtime (not serialized) --------------------------------------------
    // The voice this source currently drives. Generational, so a stale id
    // (the voice was stolen or retired) resolves to nothing and AudioSystem
    // treats the source as not playing.
    VoiceId Voice{};
    // One-shots fire once per component lifetime; loops use Voice validity.
    bool Started = false;
};

// Archetype storage relocates components with memcpy, so the component must
// stay trivially copyable (enforced by World::RegisterComponent) — this is
// why Bus is an InlineString, not std::string.

#if !defined(SENCHA_CODEGEN)
#  include <audio/AudioSourceComponent.sencha.h>
#endif

// Adding retains the clip and nothing more: deserialization is not
// activation, and the zone may be dormant. AudioSystem starts playback.
//
// OnRemove enforces the slice's one invariant (docs/audio/runtime.md,
// Decision C): a voice never outlives the clip reference that feeds it. Stop
// first, then release -- in that order, in this hook, which fires on both
// entity destruction and zone detach.
template <>
struct ComponentTraits<AudioSourceComponent> : SchemaAssetOwnership<AudioSourceComponent>
{
    static void OnRemove(const AudioSourceComponent& component, World& world, EntityId);
};
