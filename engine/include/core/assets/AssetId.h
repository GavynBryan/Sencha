#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <core/identity/StrongId.h>

// Stable 64-bit identity the asset carries through every move; zero is invalid.
// Its text form is 16 lowercase hex digits, since a JSON number cannot hold 64
// bits (docs/assets/pipeline.md, Decision A).
using AssetId = StrongId<struct AssetIdTag, uint64_t>;

// 16-digit lowercase hex, no prefix: "00000000000001a4".
[[nodiscard]] std::string AssetIdToString(AssetId id);

// Strict inverse of AssetIdToString: exactly 16 hex digits, nonzero.
// Anything else — wrong length, stray characters, the invalid id — is
// nullopt; a malformed id in content should fail loudly at parse time.
[[nodiscard]] std::optional<AssetId> AssetIdFromString(std::string_view text);

// The id an asset at `path` is born with: the same on every machine and branch
// that sees it there first. A nonzero `salt` mints another, for the rare path
// whose id is already taken.
[[nodiscard]] AssetId AssetIdOfPath(std::string_view path, uint64_t salt = 0);

// The id of a part of a file (`file#anim:Walk`), derived from the file's id so
// it follows the file through a move.
[[nodiscard]] AssetId AssetIdOfPart(AssetId file, std::string_view part);
