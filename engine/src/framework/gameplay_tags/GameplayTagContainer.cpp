#include <framework/gameplay_tags/GameplayTagContainer.h>

#include <framework/gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <cassert>

namespace
{
    // First index in the sorted [0, count) range whose tag id is >= tag.
    int LowerBound(const GameplayTagId* tags, int count, GameplayTagId tag)
    {
        int lo = 0;
        int hi = count;
        while (lo < hi)
        {
            const int mid = lo + (hi - lo) / 2;
            if (tags[mid].Value < tag.Value)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }
}

bool GameplayTagContainer::HasExact(GameplayTagId tag) const
{
    const int i = LowerBound(Tags, Count, tag);
    return i < Count && Tags[i] == tag;
}

std::uint16_t GameplayTagContainer::StackCount(GameplayTagId tag) const
{
    const int i = LowerBound(Tags, Count, tag);
    return (i < Count && Tags[i] == tag) ? Counts[i] : std::uint16_t{ 0 };
}

bool GameplayTagContainer::Grant(GameplayTagId tag, std::uint16_t stacks)
{
    if (stacks == 0 || !tag.IsValid())
        return false;

    const int i = LowerBound(Tags, Count, tag);
    if (i < Count && Tags[i] == tag)
    {
        const std::uint32_t sum = static_cast<std::uint32_t>(Counts[i]) + stacks;
        Counts[i] = static_cast<std::uint16_t>(std::min<std::uint32_t>(sum, 0xFFFFu));
        return false; // already present
    }

    if (Count >= Capacity)
    {
        assert(false && "GameplayTagContainer capacity exceeded");
        return false;
    }

    for (int j = Count; j > i; --j)
    {
        Tags[j] = Tags[j - 1];
        Counts[j] = Counts[j - 1];
    }
    Tags[i] = tag;
    Counts[i] = stacks;
    ++Count;
    return true; // newly present
}

bool GameplayTagContainer::Revoke(GameplayTagId tag, std::uint16_t stacks)
{
    if (stacks == 0)
        return false;

    const int i = LowerBound(Tags, Count, tag);
    if (i >= Count || !(Tags[i] == tag))
        return false;

    if (Counts[i] > stacks)
    {
        Counts[i] = static_cast<std::uint16_t>(Counts[i] - stacks);
        return false; // still present
    }

    for (int j = i; j + 1 < Count; ++j)
    {
        Tags[j] = Tags[j + 1];
        Counts[j] = Counts[j + 1];
    }
    --Count;
    return true; // newly absent
}

bool GameplayTagContainer::HasDescendantOf(const GameplayTagRegistry& registry,
                                           GameplayTagId ancestor) const
{
    for (int i = 0; i < Count; ++i)
        if (registry.IsDescendantOf(Tags[i], ancestor))
            return true;
    return false;
}
