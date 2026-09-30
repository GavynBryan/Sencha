#include "shell/TransientSurfaces.h"

#include <algorithm>

TransientSurfaces::Id TransientSurfaces::Open(TransientSpec spec)
{
    const Id id = NextId++;
    Entries.push_back(Entry{ .Key = id, .Spec = std::move(spec), .Order = NextOrder++ });
    return id;
}

void TransientSurfaces::Close(Id id)
{
    if (Find(id) == nullptr)
        return;
    // Children first, so an observer never hears of a child after its parent.
    std::vector<Id> children;
    for (const Entry& entry : Entries)
        if (entry.Spec.Parent == id)
            children.push_back(entry.Key);
    for (const Id child : children)
        Close(child);
    std::erase_if(Entries, [id](const Entry& entry) { return entry.Key == id; });
    if (ClosedObserver)
        ClosedObserver(id);
}

void TransientSurfaces::CloseOwnedBy(std::string_view owner)
{
    std::vector<Id> owned;
    for (const Entry& entry : Entries)
        if (entry.Spec.Owner == owner)
            owned.push_back(entry.Key);
    for (const Id id : owned)
        Close(id);
}

void TransientSurfaces::SetPinned(Id id, bool pinned)
{
    for (Entry& entry : Entries)
        if (entry.Key == id)
            entry.Pinned = pinned;
}

bool TransientSurfaces::IsPinned(Id id) const
{
    const Entry* entry = Find(id);
    return entry != nullptr && entry->Pinned;
}

bool TransientSurfaces::IsOpen(Id id) const
{
    return Find(id) != nullptr;
}

std::vector<TransientSurfaces::Id> TransientSurfaces::OpenIds() const
{
    std::vector<Id> ids;
    ids.reserve(Entries.size());
    for (const Entry& entry : Entries)
        ids.push_back(entry.Key);
    return ids;
}

bool TransientSurfaces::Escape()
{
    const Entry* target = nullptr;
    for (const Entry& entry : Entries)
    {
        if (entry.Pinned || entry.Spec.Dismiss == TransientDismiss::OwnerBound)
            continue;
        if (target == nullptr || entry.Spec.Priority > target->Spec.Priority
            || (entry.Spec.Priority == target->Spec.Priority && entry.Order > target->Order))
            target = &entry;
    }
    if (target == nullptr)
        return false;
    Close(target->Key);
    return true;
}

void TransientSurfaces::PressedOutside(const std::vector<Id>& inside)
{
    std::vector<Id> dismissed;
    for (const Entry& entry : Entries)
    {
        if (entry.Pinned || entry.Spec.Dismiss != TransientDismiss::Light)
            continue;
        const bool spared = std::any_of(inside.begin(), inside.end(), [&](Id hit) {
            return hit == entry.Key || IsAncestorOf(entry.Key, hit);
        });
        if (!spared)
            dismissed.push_back(entry.Key);
    }
    for (const Id id : dismissed)
        Close(id);
}

const TransientSurfaces::Entry* TransientSurfaces::Find(Id id) const
{
    for (const Entry& entry : Entries)
        if (entry.Key == id)
            return &entry;
    return nullptr;
}

bool TransientSurfaces::IsAncestorOf(Id ancestor, Id id) const
{
    for (const Entry* entry = Find(id); entry != nullptr; entry = Find(entry->Spec.Parent))
        if (entry->Spec.Parent == ancestor)
            return true;
    return false;
}
