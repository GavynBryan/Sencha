#pragma once

#include <audio/AudioSourceComponent.h>
#include <audio/Caption.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

//=============================================================================
// AudioCaptionComponent (docs/audio/captions-and-dialogue.md, Decision D)
//
// Composition, not a parallel emitter: this component sits on the same
// entity as an AudioSourceComponent and gives that source's playback a
// caption. The caption lives on the *placement*, which is where the meaning
// lives — the same clip can carry a caption on one entity and stay decorative
// on another. Raw sources without this component never emit captions.
//
// CaptionSystem drives it (Decision E): when the sibling source's voice
// starts, a voice-bound caption begins; when playback failed outright, the
// Decision C degrade path runs once. The authored fields are the schema; the
// runtime fields default-initialize on load.
//=============================================================================
struct SENCHA_COMPONENT("AudioCaption")
       SENCHA_SCHEMA("AudioCaption")
       SENCHA_SCENE_CHUNK("ACAP")
AudioCaptionComponent
{
    // -- Authored (serialized) -----------------------------------------------
    SENCHA_FIELD("kind")
    CaptionKind Kind = CaptionKind::ClosedCaption;

    SENCHA_FIELD("channel")
    CaptionChannelName Channel = "World";

    SENCHA_FIELD("priority")
    CaptionPriority Priority = CaptionPriority::Gameplay;

    SENCHA_FIELD("text")
    CaptionTextKey Text;

    SENCHA_FIELD("speaker")
    SpeakerKey Speaker{};             // empty = no speaker tag

    SENCHA_FIELD("duration_seconds")
    float DurationSeconds = 0.0f;   // 0 = derive from voice/clip; loops should
                                    // author finite unless persistent is meant

    SENCHA_FIELD("merge_duplicates")
    bool MergeDuplicates = true;

    // -- Runtime (not serialized) ----------------------------------------------
    // The active caption this component drives, and the voice it was begun
    // for. A fresh voice on the sibling source (loop restart after zone
    // re-entry) differs from CaptionedVoice and re-captions; a one-shot's
    // latched stale voice matches and does not.
    CaptionId Caption{};
    VoiceId CaptionedVoice{};
    // The degrade path (voice never started) fires once per component
    // lifetime, mirroring the one-shot Started latch.
    bool CaptionAttempted = false;
    // Warn-once latch for a caption component without a sibling source.
    bool WarnedOrphan = false;
};

// No OnAdd: there is no asset edge to retain, and deserialization is not
// activation -- CaptionSystem begins captions.
//
// OnRemove ends the active caption (entity destruction and zone detach both
// fire it). Stale-safe: an already-retired caption is a no-op, and ordering
// against the sibling source's own OnRemove does not matter.
template <>
struct ComponentTraits<AudioCaptionComponent>
{
    static void OnRemove(const AudioCaptionComponent& component, World& world, EntityId);
};

// Archetype storage relocates components with memcpy, so the component must
// stay trivially copyable (enforced by World::RegisterComponent) — this is
// why the names are InlineStrings.

#if !defined(SENCHA_CODEGEN)
#  include <audio/AudioCaptionComponent.sencha.h>
#endif
