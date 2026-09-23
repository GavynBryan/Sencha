#pragma once

//=============================================================================
// AuthoredHandle
//
// A name resolved against one catalog: which catalog, which slot, and which
// revision of the contract the resolver read. What a compiled consumer stores
// in place of a name.
//
// A slot number alone is not an identity. Slots are dense and local to one
// catalog, so slot 1 in one World's catalog is an unrelated entry in
// another's, and a slot whose contract has since changed holds a different
// shape under the same number. A handle carries all three, and every
// dispatcher that takes one checks all three before acting on it: a handle
// from another catalog, or for a contract that has moved, is refused as stale
// rather than applied to whatever occupies the slot now.
//=============================================================================
template<typename CatalogId, typename Id, typename Revision>
struct AuthoredHandle
{
    CatalogId Catalog{};
    Id Slot{};
    Revision Contract{};

    [[nodiscard]] bool IsValid() const
    {
        return Catalog.IsValid() && Slot.IsValid() && Contract.IsValid();
    }

    friend bool operator==(const AuthoredHandle&, const AuthoredHandle&) = default;
};
