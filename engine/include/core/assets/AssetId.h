#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <core/identity/StrongId.h>

//=============================================================================
// AssetId (docs/assets/pipeline.md, Decision A)
//
// Stable 64-bit asset identity, assigned by the cook step and persisted in
// the id map (AssetIdMap). Ids survive renames — the map keeps the original
// id when a source moves — while paths remain the human-facing alias and
// the dev-build fallback resolver.
//
// AssetId is a StrongId<Tag, uint64_t> — the engine's single id vocabulary —
// so equality, ordering, hashing, and binary serialization come from there.
// What is specific to AssetId is its text form: ids round-trip through 16-digit
// lowercase hex (JSON numbers are doubles and cannot hold 64 bits — the same
// rule the cooked-cache index follows for content hashes).
//
// Zero is the invalid id.
//=============================================================================
using AssetId = StrongId<struct AssetIdTag, uint64_t>;

// 16-digit lowercase hex, no prefix: "00000000000001a4".
[[nodiscard]] std::string AssetIdToString(AssetId id);

// Strict inverse of AssetIdToString: exactly 16 hex digits, nonzero.
// Anything else — wrong length, stray characters, the invalid id — is
// nullopt; a malformed id in content should fail loudly at parse time.
[[nodiscard]] std::optional<AssetId> AssetIdFromString(std::string_view text);
