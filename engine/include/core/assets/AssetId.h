#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// AssetId (docs/assets/pipeline.md, Decision A)
//
// Stable 64-bit asset identity, assigned by the cook step and persisted in
// the id map (AssetIdMap). Ids survive renames — the map keeps the original
// id when a source moves — while paths remain the human-facing alias and
// the dev-build fallback resolver.
//
// Zero is the invalid id. Text formats carry ids as 16-digit lowercase hex
// strings (JSON numbers are doubles and cannot hold 64 bits — the same rule
// the cooked-cache index follows for content hashes).
//=============================================================================
struct AssetId
{
    uint64_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }

    friend bool operator==(AssetId, AssetId) = default;
};

template <>
struct std::hash<AssetId>
{
    std::size_t operator()(AssetId id) const noexcept
    {
        return std::hash<uint64_t>{}(id.Value);
    }
};

// 16-digit lowercase hex, no prefix: "00000000000001a4".
[[nodiscard]] std::string AssetIdToString(AssetId id);

// Strict inverse of AssetIdToString: exactly 16 hex digits, nonzero.
// Anything else — wrong length, stray characters, the invalid id — is
// nullopt; a malformed id in content should fail loudly at parse time.
[[nodiscard]] std::optional<AssetId> AssetIdFromString(std::string_view text);
