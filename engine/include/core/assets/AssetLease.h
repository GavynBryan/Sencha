#pragma once

#include <core/assets/AssetRef.h>
#include <core/handle/ILifetimeOwner.h>

#include <cstdint>
#include <utility>

//=============================================================================
// AssetLease
//
// One ref-counted reference to an asset whose concrete handle type is known
// only to the cache that issued it. Generic orchestration (preload, hot
// reload) holds leases; domain code keeps its typed handles.
//
// The lease is the reason a type-erased commit does not lose ownership: the
// token plus the issuing ILifetimeOwner is exactly what a typed handle
// carries, so releasing a lease releases the same reference the typed handle
// would have. Move-only, and Reset is idempotent.
//=============================================================================
class AssetLease
{
public:
    AssetLease() = default;

    ~AssetLease()
    {
        Reset();
    }

    AssetLease(AssetLease&& other) noexcept
        : TypeValue(other.TypeValue)
        , Owner(other.Owner)
        , Token(other.Token)
    {
        other.TypeValue = AssetType::Unknown;
        other.Owner = nullptr;
        other.Token = 0;
    }

    AssetLease& operator=(AssetLease&& other) noexcept
    {
        if (this == &other)
            return *this;

        Reset();
        TypeValue = other.TypeValue;
        Owner = other.Owner;
        Token = other.Token;
        other.TypeValue = AssetType::Unknown;
        other.Owner = nullptr;
        other.Token = 0;
        return *this;
    }

    AssetLease(const AssetLease&) = delete;
    AssetLease& operator=(const AssetLease&) = delete;

    // Takes a new reference. Use when the caller does not already hold one.
    [[nodiscard]] static AssetLease Retain(AssetType type,
                                          ILifetimeOwner& owner,
                                          uint64_t token)
    {
        if (type == AssetType::Unknown || token == 0)
            return {};

        owner.Attach(token);
        return AssetLease(type, owner, token);
    }

    // Takes over a reference the caller already holds, such as a freshly
    // created cache entry or an acquisition that already incremented the
    // count. Adopting a reference the caller does not hold double-frees.
    [[nodiscard]] static AssetLease Adopt(AssetType type,
                                         ILifetimeOwner& owner,
                                         uint64_t token)
    {
        if (type == AssetType::Unknown || token == 0)
            return {};

        return AssetLease(type, owner, token);
    }

    void Reset()
    {
        if (Owner != nullptr && Token != 0)
            Owner->Detach(Token);

        TypeValue = AssetType::Unknown;
        Owner = nullptr;
        Token = 0;
    }

    // Hands the held reference to a caller that will own it by other means --
    // a raw handle stored where the storage's own lifecycle releases it. The
    // lease is empty afterwards and the reference is NOT dropped, so a caller
    // that discards the returned token leaks it.
    [[nodiscard]] uint64_t Relinquish()
    {
        const uint64_t token = Token;
        TypeValue = AssetType::Unknown;
        Owner = nullptr;
        Token = 0;
        return token;
    }

    [[nodiscard]] bool IsValid() const
    {
        return TypeValue != AssetType::Unknown && Owner != nullptr && Token != 0;
    }

    explicit operator bool() const
    {
        return IsValid();
    }

    [[nodiscard]] AssetType Type() const
    {
        return TypeValue;
    }

    // Opaque to everyone but the issuing owner, which decodes it as its own
    // handle.
    [[nodiscard]] uint64_t OpaqueToken() const
    {
        return Token;
    }

    [[nodiscard]] ILifetimeOwner* LifetimeOwner() const
    {
        return Owner;
    }

private:
    AssetLease(AssetType type, ILifetimeOwner& owner, uint64_t token)
        : TypeValue(type)
        , Owner(&owner)
        , Token(token)
    {
    }

    AssetType TypeValue = AssetType::Unknown;
    ILifetimeOwner* Owner = nullptr;
    uint64_t Token = 0;
};
