#pragma once

#include <audio/AudioVoice.h>
#include <audio/AudioClipHandle.h>
#include <core/text/InlineString.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

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
    AudioClipHandle Clip{};
    BusName Bus = "Sfx";
    float Gain = 1.0f;          // [0, 1]
    float Pan = 0.0f;           // [-1 left, +1 right], static (no listener yet)
    bool Looping = false;
    bool PlayOnActive = true;   // emit while the zone is audio-active

    // -- Runtime (not serialized) --------------------------------------------
    // The voice this source currently drives. Generational, so a stale id
    // (the voice was stolen or retired) resolves to nothing and AudioSystem
    // treats the source as not playing.
    VoiceId Voice{};
    // One-shots fire once per component lifetime; loops use Voice validity.
    bool Started = false;
};

class CaptionRuntime;

// Archetype storage relocates components with memcpy, so the component must
// stay trivially copyable (enforced by World::RegisterComponent) — this is
// why Bus is an InlineString, not std::string.

SENCHA_DECLARE_COMPONENT_TYPE(AudioSourceComponent, "AudioSource");
SENCHA_COMPONENT_DECLARES_SCHEMA(AudioSourceComponent);
SENCHA_COMPONENT_DECLARES_TRAITS(AudioSourceComponent);
