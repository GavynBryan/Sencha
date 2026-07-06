#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <core/identity/StrongId.h>
#include <zone/ZoneId.h>

// Persistent partition identities beside ZoneId. Minted by the editor, serialized
// in the world partition manifest. Zero is invalid.
using RegionId     = StrongId<struct RegionIdTag,     uint64_t>;
using TransitionId = StrongId<struct TransitionIdTag, uint64_t>;

// Text forms follow the AssetId precedent exactly: 16-digit lowercase hex, no
// prefix, because JSON numbers are doubles and cannot hold 64 bits. FromString is
// strict: exactly 16 hex digits and nonzero, anything else is nullopt so malformed
// content fails loudly at parse time.
[[nodiscard]] std::string ZoneIdToString(ZoneId id);
[[nodiscard]] std::optional<ZoneId> ZoneIdFromString(std::string_view text);
[[nodiscard]] std::string RegionIdToString(RegionId id);
[[nodiscard]] std::optional<RegionId> RegionIdFromString(std::string_view text);
[[nodiscard]] std::string TransitionIdToString(TransitionId id);
[[nodiscard]] std::optional<TransitionId> TransitionIdFromString(std::string_view text);
