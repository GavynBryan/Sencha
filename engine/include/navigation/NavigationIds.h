#pragma once

#include <core/identity/StrongId.h>

#include <optional>
#include <string>
#include <string_view>

// Stable identity of one authored navigation link. Minted by the editor,
// serialized in scene documents and in the cooked navigation file, and the
// only identity a route, a runtime link-state binding, or an agent's memory of
// a failed traversal may hold. Zero is invalid.
using NavLinkId = StrongId<struct NavLinkIdTag, uint64_t>;

// Text form: 16 lowercase hex digits, the AssetId and DockId precedent. Strict:
// anything but exactly 16 hex digits naming a nonzero value is nullopt.
[[nodiscard]] std::string NavLinkIdToString(NavLinkId id);
[[nodiscard]] std::optional<NavLinkId> NavLinkIdFromString(std::string_view text);
