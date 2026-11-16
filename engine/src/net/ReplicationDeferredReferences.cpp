#include <net/ReplicationDeferredReferences.h>

#include <algorithm>

void ReplicationDeferredReferences::Remember(EntityId holder, ComponentTypeId type, std::span<const std::byte> image,
                                             std::span<const std::uint64_t> unresolved)
{
    const auto found = std::ranges::find_if(Entries, [&](const Waiting& waiting) {
        return waiting.Holder == holder && waiting.Type == type;
    });
    if (unresolved.empty())
    {
        if (found != Entries.end())
            Entries.erase(found);
        return;
    }
    if (found != Entries.end())
        Entries.erase(found);
    else if (Entries.size() == kCapacity)
    {
        Entries.erase(Entries.begin());
        ++Evicted;
    }
    Entries.push_back(Waiting{ holder, type, { image.begin(), image.end() }, { unresolved.begin(), unresolved.end() } });
}

const ReplicationDeferredReferences::Waiting* ReplicationDeferredReferences::Find(EntityId holder,
                                                                                 ComponentTypeId type) const
{
    const auto found = std::ranges::find_if(Entries, [&](const Waiting& waiting) {
        return waiting.Holder == holder && waiting.Type == type;
    });
    return found != Entries.end() ? &*found : nullptr;
}

void ReplicationDeferredReferences::ForgetEntity(EntityId holder)
{
    std::erase_if(Entries, [&](const Waiting& waiting) { return waiting.Holder == holder; });
}
