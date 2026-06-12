#pragma once

#include <audio/AudioVoice.h>
#include <core/text/InlineString.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <optional>
#include <string_view>

//=============================================================================
// Caption vocabulary (docs/audio/captions-and-dialogue.md, Decision A)
//
// Kind and priority are engine enums: they encode accessibility filtering and
// trim ordering, which the engine implements. The presentation *channel* is a
// game-defined name resolved against CaptionConfig at runtime — the bus
// pattern. The engine never interprets channel names beyond routing.
//=============================================================================

enum class CaptionKind : uint8_t
{
    None,           // never stored; "this sound has no text semantics"
    Subtitle,       // spoken dialogue or intelligible voice
    ClosedCaption,  // meaningful non-speech sound
};

enum class CaptionPriority : uint8_t
{
    Ambient,
    Gameplay,
    Narrative,
    Critical,
};

// Channel and text identifiers live inside ECS components, so they are
// fixed-capacity inline strings (archetype storage relocates components with
// memcpy — see InlineString).
using CaptionChannelName = InlineString<32>;
using CaptionTextKey = InlineString<64>;  // localization key by intent; the
                                          // engine never interprets it
using SpeakerKey = InlineString<64>;

// Enum <-> string helpers shared by the scene codec, config, and tests.
// Scene JSON carries author-readable strings ("kind": "Subtitle"), never
// raw integers.
[[nodiscard]] std::string_view CaptionKindToString(CaptionKind kind);
[[nodiscard]] std::optional<CaptionKind> CaptionKindFromString(std::string_view text);
[[nodiscard]] std::string_view CaptionPriorityToString(CaptionPriority priority);
[[nodiscard]] std::optional<CaptionPriority> CaptionPriorityFromString(std::string_view text);

//=============================================================================
// CaptionId
//
// Opaque generational handle returned by CaptionRuntime::Begin*. Same
// contract as VoiceId: a zero Id means "invalid / no caption", stale handles
// from a previous caption occupying the same slot are safely rejected, and
// ending an invalid or already-ended caption is a no-op.
//=============================================================================
struct CaptionId
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    bool operator==(const CaptionId&) const = default;
};

//=============================================================================
// CaptionPayload
//
// The authored request: what text, on which channel, how it filters and
// trims. DurationSeconds == 0 means "derive": voice lifetime when a voice is
// bound, otherwise clip duration or a reading-speed estimate, clamped by
// CaptionSettings (Decision C).
//=============================================================================
struct CaptionPayload
{
    CaptionKind Kind = CaptionKind::None;
    CaptionChannelName Channel = "World";
    CaptionPriority Priority = CaptionPriority::Gameplay;

    CaptionTextKey Text;
    SpeakerKey Speaker;       // empty = no speaker tag

    float DurationSeconds = 0.0f;
    bool MergeDuplicates = true;
};

//=============================================================================
// CaptionSource
//
// Where the caption came from, for presenters that want it (speaker
// portraits, offscreen direction arrows later).
//=============================================================================
struct CaptionSource
{
    EntityId Entity;
    // Later: ZoneId, world position, portrait id, faction, radio channel.
};

//=============================================================================
// CaptionSettings
//
// Accessibility state, engine-owned data only — UI mirrors it into menus.
// Per-surface presentation policy (line counts, merging) lives in
// CaptionChannelConfig instead, because surfaces genuinely differ.
//=============================================================================
struct CaptionSettings
{
    bool SubtitlesEnabled = true;
    bool ClosedCaptionsEnabled = false;
    bool SpeakerNamesEnabled = true;  // presenter hint; the engine never renders

    // Clamps for durations the runtime derives when no explicit duration or
    // voice lifetime is available (Decision C's degrade path).
    float DerivedDurationMinSeconds = 1.25f;
    float DerivedDurationMaxSeconds = 6.0f;
};

//=============================================================================
// ActiveCaption
//
// Runtime record held by CaptionRuntime while a caption lives. Sequence is a
// monotonic begin counter: it makes visible-list ordering deterministic and
// lets presenters re-sort chronologically if they prefer.
//=============================================================================
struct ActiveCaption
{
    CaptionId Id;
    VoiceId Voice;            // invalid for timed captions
    CaptionPayload Payload;
    CaptionSource Source;
    uint64_t Sequence = 0;
    float AgeSeconds = 0.0f;
    float DurationSeconds = 0.0f;  // resolved; 0 = voice-bound, no cap
};
