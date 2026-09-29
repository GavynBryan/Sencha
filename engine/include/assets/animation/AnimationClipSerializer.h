#pragma once

#include <anim/AnimationClip.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// The .sanim round trip, both halves pure. Layout and versions are in
// docs/assets/pipeline.md.

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
