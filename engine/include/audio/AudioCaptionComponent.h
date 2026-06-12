#pragma once

#include <audio/AudioSourceComponent.h>
#include <audio/Caption.h>
#include <audio/CaptionRuntime.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

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
    CaptionTextKey Text;
    SpeakerKey Speaker;             // empty = no speaker tag
    float DurationSeconds = 0.0f;   // 0 = derive from voice/clip; loops should
                                    // author finite unless persistent is meant
    bool MergeDuplicates = true;

    // -- Runtime (not serialized) ----------------------------------------------
    // The active caption this component drives, and the voice it was begun
    // for. A fresh voice on the sibling source (loop restart after zone
    // re-entry) differs from CaptionedVoice and re-captions; a one-shot's
    // latched stale voice matches and does not.
    CaptionId Caption;
    VoiceId CaptionedVoice;
    // The degrade path (voice never started) fires once per component
    // lifetime, mirroring the one-shot Started latch.
    bool CaptionAttempted = false;
    // Warn-once latch for a caption component without a sibling source.
    bool WarnedOrphan = false;
};

// Archetype storage relocates components with memcpy, so the component must
// stay trivially copyable — this is why the names are InlineStrings.
static_assert(std::is_trivially_copyable_v<AudioCaptionComponent>,
              "AudioCaptionComponent must be trivially copyable for ECS chunk storage");

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
