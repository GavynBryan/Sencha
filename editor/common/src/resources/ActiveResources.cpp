#include "resources/ActiveResources.h"

#include <utility>

namespace
{
const AssetRef kNothing{};
const ActiveResources::StashSlots kEmptyStash{};
} // namespace

const AssetRef& ActiveResources::Active(AssetType type) const
{
    const auto it = Held.find(type);
    return it != Held.end() ? it->second.Active : kNothing;
}

void ActiveResources::SetActive(const AssetRef& ref)
{
    Holding& holding = Held[ref.Type];
    if (holding.Active.Type == ref.Type && holding.Active.Path == ref.Path)
        return;
    holding.Active = ref;
    Observers.Notify(ref.Type);
}

const ActiveResources::StashSlots& ActiveResources::Stash(AssetType type) const
{
    const auto it = Held.find(type);
    return it != Held.end() ? it->second.Stash : kEmptyStash;
}

void ActiveResources::UseStashSlot(AssetType type, std::size_t slot)
{
    if (slot >= kStashSlots)
        return;
    Holding& holding = Held[type];
    AssetRef& stashed = holding.Stash[slot];
    if (!stashed.IsValid())
    {
        stashed = holding.Active;
        return;
    }
    std::swap(stashed, holding.Active);
    Observers.Notify(type);
}

void ActiveResources::StoreInStash(AssetType type, std::size_t slot)
{
    if (slot < kStashSlots)
        Held[type].Stash[slot] = Active(type);
}

void ActiveResources::ClearStashSlot(AssetType type, std::size_t slot)
{
    if (slot < kStashSlots)
        Held[type].Stash[slot] = {};
}
