#pragma once

#include <core/assets/AssetLease.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// IAssetStore
//
// The residency half of an asset kind, in terms no consumer has to type
// against: is this path resident, take a reference to it, what path does this
// token name. AssetCache supplies all of it, so a concrete cache satisfies the
// seam by naming its AssetType; the typed API each cache exposes to domain
// code is untouched.
//
// This exists so preload and hot reload stop carrying one branch per asset
// kind. Both are driven by a registry record, not by a switch over AssetType.
//=============================================================================
class IAssetStore
{
public:
    virtual ~IAssetStore() = default;

    [[nodiscard]] virtual AssetType Type() const = 0;
    [[nodiscard]] virtual bool IsResident(std::string_view path) const = 0;

    // Cached-only: a lease if `path` is resident, an invalid lease otherwise.
    // Never loads.
    [[nodiscard]] virtual AssetLease TryAcquireLease(std::string_view path) = 0;

    [[nodiscard]] virtual std::string_view GetPath(uint64_t token) const = 0;
};
