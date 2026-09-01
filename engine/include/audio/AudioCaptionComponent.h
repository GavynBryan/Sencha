#pragma once

#include <audio/AudioSourceComponent.h>
#include <audio/Caption.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <string_view>
#include <tuple>
#include <type_traits>

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
// Decision C degrade path runs once. Authored fields serialize through
// TypeSchema; the runtime fields default-initialize on load.
//=============================================================================
struct AudioCaptionComponent
{
    // -- Authored (serialized) -----------------------------------------------
    CaptionKind Kind = CaptionKind::ClosedCaption;
    CaptionChannelName Channel = "World";
    CaptionPriority Priority = CaptionPriority::Gameplay;
    CaptionTextKey Text{};
    SpeakerKey Speaker{};             // empty = no speaker tag
    float DurationSeconds = 0.0f;   // 0 = derive from voice/clip; loops should
                                    // author finite unless persistent is meant
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

// Archetype storage relocates components with memcpy, so the component must
// stay trivially copyable (enforced by World::RegisterComponent) — this is
// why the names are InlineStrings.

SENCHA_DECLARE_COMPONENT_TYPE(AudioCaptionComponent, "AudioCaption");
