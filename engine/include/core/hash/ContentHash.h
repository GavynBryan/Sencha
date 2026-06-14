#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

//=============================================================================
// ContentHash (docs/assets/pipeline.md, Decision A)
//
// 64-bit content hashing for asset bytes — the one shared mechanism behind
// cooked-data invalidation (Decision B) and hot-reload change detection
// (Decision H). The algorithm is XXH64: fast, well-distributed, and stable
// for a given byte sequence, which is all the asset layer asks of it.
//
// Not cryptographic. Never use this where an adversary controls the input
// and a collision has security consequences.
//
// The implementation assumes a little-endian target (every platform Sencha
// ships on). Hashes are compared against values produced on the same class
// of machine — the cooked cache is per-checkout, not distributed.
//=============================================================================

[[nodiscard]] uint64_t HashBytes64(std::span<const std::byte> bytes, uint64_t seed = 0);
[[nodiscard]] uint64_t HashBytes64(std::string_view text, uint64_t seed = 0);

// Reads the file at `filePath` and hashes its contents. Returns false on
// read failure (missing file, IO error); `outHash` is left untouched.
[[nodiscard]] bool HashFileContents(std::string_view filePath, uint64_t& outHash);
