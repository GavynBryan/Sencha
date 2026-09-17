#include <app/BackRouter.h>

#include <algorithm>
#include <cstring>
#include <utility>

BackConsumerLease BackRouter::AddConsumer(std::string_view name,
                                          BackPriority priority,
                                          Consumer consumer)
{
    if (!consumer)
        return {};

    // Slots are reused rather than compacted: a dispatch may be running, and
    // erasing from under it would invalidate the walk.
    std::size_t index = Entries.size();
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        if (!Entries[i].Live)
        {
            index = i;
            break;
        }
    }
    if (index == Entries.size())
        Entries.emplace_back();

    Entries[index] = Entry{
        .Name = std::string(name),
        .Priority = priority,
        .Handle = std::move(consumer),
        .Sequence = NextSequence++,
        .Live = true,
    };

    return BackConsumerLease(this, BackConsumerToken{ static_cast<std::uint32_t>(index + 1) });
}

bool BackRouter::Dispatch()
{
    // Built per dispatch rather than kept sorted: Back fires on a keypress, not
    // per frame, and a handful of entries is not worth the bookkeeping a sorted
    // structure would need across registration and release.
    std::vector<const Entry*> order;
    order.reserve(Entries.size());
    for (const Entry& entry : Entries)
    {
        if (entry.Live && entry.Handle)
            order.push_back(&entry);
    }

    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
        if (a->Priority != b->Priority)
            return a->Priority < b->Priority;
        // Most recently registered first, which is what makes a modal spawned
        // by an inventory close before the inventory does.
        return a->Sequence > b->Sequence;
    });

    for (const Entry* entry : order)
    {
        // Re-checked: an earlier consumer may have closed something that
        // dropped a later one's lease.
        if (!entry->Live || !entry->Handle)
            continue;
        if (entry->Handle())
            return true;
    }
    return false;
}

std::size_t BackRouter::ConsumerCount() const
{
    std::size_t live = 0;
    for (const Entry& entry : Entries)
        live += entry.Live ? 1u : 0u;
    return live;
}

void BackRouter::Attach(std::uint64_t)
{
    // Registration already made the slot live. A Back consumer is one holder's,
    // not a counted resource: two owners of one lease would be two objects
    // believing they decide when it goes away.
}

void BackRouter::Detach(std::uint64_t token)
{
    // The slot Owned encoded, read back the way the other lease owners do.
    std::uint32_t value = 0;
    std::memcpy(&value, &token, sizeof(value));
    if (value == 0 || value > Entries.size())
        return;
    Entry& entry = Entries[value - 1];
    entry.Live = false;
    // Released here rather than at reuse, so a lambda's captures do not outlive
    // the thing that registered it.
    entry.Handle = {};
    entry.Name.clear();
}
