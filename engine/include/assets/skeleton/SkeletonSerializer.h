#pragma once

#include <anim/Skeleton.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// .sskel round trip (docs/assets/pipeline.md, Decision J).
//
// Both halves are pure functions (no logging, no engine state) — the .sclip
// precedent: the write half runs inside importers, the read half inside the
// stage half of the loader, possibly on a task thread.
//
// Layout (version 1, little-endian):
//   magic 'SSKL', u32 version, u32 jointCount, u32 reserved(0)
//   per joint: u32 nameLength, name bytes, i32 parentIndex,
//              f32 bindTranslation[3], f32 bindRotation[4] (xyzw),
//              f32 bindScale[3], f32 inverseBind[16] (row-major)
//=============================================================================

inline constexpr uint32_t kSskelFormatVersion = 1;

// Writes `skeleton` as a .sskel container. Returns false (and reports via
// `error`) when the skeleton fails ValidateSkeletonData.
[[nodiscard]] bool WriteSskelToBytes(const SkeletonData& skeleton,
                                     std::vector<std::byte>& out,
                                     std::string* error = nullptr);

// Parses a .sskel container. A malformed container is rejected, never
// patched; the result is re-validated so every producer meets the same
// invariants. Errors travel in `error`.
[[nodiscard]] bool LoadSskelFromBytes(std::span<const std::byte> bytes,
                                      SkeletonData& out,
                                      std::string* error = nullptr);
