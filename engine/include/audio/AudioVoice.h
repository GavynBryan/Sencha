#pragma once

#include <core/handle/Handle.h>

#include <cstdint>

//=============================================================================
// VoiceId
//
// Opaque generational handle returned by AudioService::Play() into the voice
// slot pool, so a stale handle from a previous voice occupying the same slot
// is safely rejected. One of the engine's unified Handle<Tag> types (handle
// convergence) — transient, never persisted.
//
// A null VoiceId means "invalid / not playing". Check IsValid() before passing
// to any AudioService method.
//=============================================================================
using VoiceId = Handle<struct VoiceTag>;

//=============================================================================
// AudioClipKey
//
// Diagnostic token identifying which clip a voice is playing — in practice
// the AudioClipHandle index. Not an asset identity: the stable, persisted
// AssetId lives in core/assets/AssetId.h (docs/assets/pipeline.md,
// Decision A) and has nothing to do with voice bookkeeping.
//=============================================================================
struct AudioClipKey
{
    uint32_t Value = 0;

    bool operator==(const AudioClipKey&) const = default;
    explicit operator bool() const { return Value != 0; }
};

//=============================================================================
// VoiceState
//
// Discriminated playback state. Idle means the voice slot is unoccupied and
// can be freely reused. Playing, Paused, and Stopped are mutually exclusive
// active states for an occupied slot.
//=============================================================================
enum class VoiceState : uint8_t
{
    Idle,
    Playing,
    Paused,
    Stopped,
};
