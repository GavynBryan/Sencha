#pragma once

#include <cstdint>

//=============================================================================
// Handle<Tag>
//
// One generational slot reference, the engine's single opaque-handle type.
// The phantom `Tag` makes each resource's handle a distinct, non-
// interchangeable type with zero per-handle boilerplate:
//
//   using TextureHandle = Handle<struct TextureHandleTag>;
//   using MaterialHandle = Handle<struct MaterialHandleTag>;
//
// Layout is a split { index, generation }, both 32-bit. The 32-bit generation
// is deliberate: a packed handle with a narrow generation field wraps after a
// few thousand slot recycles, and a stale handle from a slot's earlier life
// can then alias its new occupant — an ABA hazard latent in exactly Sencha's
// streaming-with-churn + hot-reload workload. A 32-bit generation makes
// wraparound unreachable. Handles are passed around, not stored by the
// million, so 8 bytes is free.
//
// `0` is the null value (index 0 is the reserved null slot in every pool).
// Handles are transient runtime references — they carry no reflection and are
// never serialized. Persisted references use a stable identity (StrongId /
// asset path), resolved to a handle at load time.
//=============================================================================
template <typename Tag>
struct Handle
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != 0 && Generation != 0; }
    [[nodiscard]] bool IsNull() const { return !IsValid(); }

    bool operator==(const Handle&) const = default;
};

// The slot index of a valid handle, for code that indexes a parallel array by
// slot (e.g. per-instance draw data). Free function rather than a method so it
// is uniform across every Handle<Tag>.
template <typename Tag>
[[nodiscard]] uint32_t SlotIndex(Handle<Tag> handle)
{
    return handle.Index;
}
