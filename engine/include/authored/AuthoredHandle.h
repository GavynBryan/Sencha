#pragma once

// A name resolved against one catalog. Slots are catalog-local and a slot's
// contract can change, so dispatchers refuse a handle from another catalog or
// an older contract instead of acting on whatever the slot holds now.
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
