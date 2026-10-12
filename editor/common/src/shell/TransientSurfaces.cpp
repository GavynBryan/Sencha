#include "shell/TransientSurfaces.h"

#include <algorithm>

TransientSurfaces::Id TransientSurfaces::Open(int priority)
{
    const Id id = NextId++;
    Entries.push_back(Entry{ .Key = id, .Priority = priority, .Order = NextOrder++ });
    return id;
}

void TransientSurfaces::Close(Id id)
{
    if (std::erase_if(Entries, [id](const Entry& entry) { return entry.Key == id; }) == 0)
        return;
    if (ClosedObserver)
        ClosedObserver(id);
}

bool TransientSurfaces::IsOpen(Id id) const
{
    return std::ranges::any_of(Entries, [id](const Entry& entry) { return entry.Key == id; });
}

bool TransientSurfaces::Escape()
{
    const auto target = std::ranges::max_element(Entries, [](const Entry& a, const Entry& b) {
        return a.Priority != b.Priority ? a.Priority < b.Priority : a.Order < b.Order;
    });
    if (target == Entries.end())
        return false;
    Close(target->Key);
    return true;
}
