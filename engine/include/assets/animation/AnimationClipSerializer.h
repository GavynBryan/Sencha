#pragma once

#include <anim/AnimationClip.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// .sanim round trip (docs/assets/pipeline.md, Decision J).
//
// Both halves are pure functions (no logging, no engine state) — the .sclip
// precedent. The container stores the authored keyframes as the cook
// extracted them; sampled storage and compression are the animation runtime
// plan's decisions, and this version field is the room they move in.
//
// Layout (version 2, little-endian):
//   magic 'SANM', u32 version, u32 trackCount, f32 durationSeconds,
//   u32 skeletonPathLength, u32 reserved(0), skeleton path bytes
//   per track: u32 jointIndex, u32 channelPath, u32 interpolation,
//              u32 keyCount, f32 times[keyCount],
//              f32 values[keyCount * components]
//   u32 eventCount, then per event:
//              u32 key, f32 time, u8 scope, u8 hasMinWeight, u16 inputCount,
//              f32 minWeight, string name, string binding,
//              per input: string name, u8 source (0 constant, 1 tag),
//                         string value (a constant as JSON text, a tag's name)
//   where a string is u32 length + bytes.
//
//   u8 hasRoot, then when set: u32 keyCount, f32 times[keyCount],
//              f32 values[keyCount * 3] (x, z, yaw)
//
// Version 1 is version 2 without the event block, and still loads with no
// events: a clip cooked before events existed has none to lose. Version 2 is
// version 3 without the root block, and loads with no root curve.
//=============================================================================

inline constexpr uint32_t kSanimFormatVersion = 3;

// Writes `clip` as a .sanim container. Returns false (and reports via
// `error`) when the clip fails ValidateAnimationClipData.
[[nodiscard]] bool WriteSanimToBytes(const AnimationClipData& clip,
                                     std::vector<std::byte>& out,
                                     std::string* error = nullptr);

// Parses a .sanim container. A malformed container is rejected, never
// patched; the result is re-validated so every producer meets the same
// invariants. Errors travel in `error`.
[[nodiscard]] bool LoadSanimFromBytes(std::span<const std::byte> bytes,
                                      AnimationClipData& out,
                                      std::string* error = nullptr);
