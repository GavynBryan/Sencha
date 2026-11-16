#include <gameplay_tags/GameplayTagContainer.h>

#include <gameplay_tags/GameplayTagRegistry.h>

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

namespace
{
    int Insert(GameplayTagContainer& tags, GameplayTagId tag)
    {
        const int i = LowerBound(tags.Tags, tags.Count, tag);
        if (i < tags.Count && tags.Tags[i] == tag)
            return i;
        if (tags.Count >= GameplayTagContainer::Capacity)
        {
            assert(false && "GameplayTagContainer capacity exceeded");
            return -1;
        }
        for (int j = tags.Count; j > i; --j)
        {
            tags.Tags[j] = tags.Tags[j - 1];
            tags.Counts[j] = tags.Counts[j - 1];
            tags.Baseline[j] = tags.Baseline[j - 1];
        }
        tags.Tags[i] = tag;
        tags.Counts[i] = 0;
        tags.Baseline[i] = 0;
        ++tags.Count;
        return i;
    }

    void RemoveAt(GameplayTagContainer& tags, int i)
    {
        for (int j = i; j + 1 < tags.Count; ++j)
        {
            tags.Tags[j] = tags.Tags[j + 1];
            tags.Counts[j] = tags.Counts[j + 1];
            tags.Baseline[j] = tags.Baseline[j + 1];
        }
        --tags.Count;
    }

    std::uint16_t Saturating(std::uint16_t held, std::uint16_t added)
    {
        return static_cast<std::uint16_t>(std::min<std::uint32_t>(std::uint32_t{ held } + added, 0xFFFFu));
    }

    bool Withdraw(GameplayTagContainer& tags, GameplayTagId tag, std::uint16_t stacks, bool fromBaseline)
    {
        if (stacks == 0)
            return false;
        const int i = LowerBound(tags.Tags, tags.Count, tag);
        if (i >= tags.Count || !(tags.Tags[i] == tag))
            return false;
        const std::uint16_t available =
            fromBaseline ? tags.Baseline[i] : static_cast<std::uint16_t>(tags.Counts[i] - tags.Baseline[i]);
        const std::uint16_t taken = std::min(stacks, available);
        tags.Counts[i] = static_cast<std::uint16_t>(tags.Counts[i] - taken);
        if (fromBaseline)
            tags.Baseline[i] = static_cast<std::uint16_t>(tags.Baseline[i] - taken);
        if (tags.Counts[i] > 0)
            return false;
        RemoveAt(tags, i);
        return true;
    }

    bool Add(GameplayTagContainer& tags, GameplayTagId tag, std::uint16_t stacks, bool toBaseline)
    {
        if (stacks == 0 || !tag.IsValid())
            return false;
        const std::uint8_t before = tags.Count;
        const int i = Insert(tags, tag);
        if (i < 0)
            return false;
        tags.Counts[i] = Saturating(tags.Counts[i], stacks);
        if (toBaseline)
            tags.Baseline[i] = Saturating(tags.Baseline[i], stacks);
        return tags.Count != before;
    }
}

bool GameplayTagContainer::Grant(GameplayTagId tag, std::uint16_t stacks)
{
    return Add(*this, tag, stacks, false);
}

bool GameplayTagContainer::Revoke(GameplayTagId tag, std::uint16_t stacks)
{
    return Withdraw(*this, tag, stacks, false);
}

bool GameplayTagContainer::GrantBaseline(GameplayTagId tag, std::uint16_t stacks)
{
    return Add(*this, tag, stacks, true);
}

bool GameplayTagContainer::RevokeBaseline(GameplayTagId tag, std::uint16_t stacks)
{
    return Withdraw(*this, tag, stacks, true);
}

bool GameplayTagContainer::Erase(GameplayTagId tag)
{
    const int i = LowerBound(Tags, Count, tag);
    if (i >= Count || !(Tags[i] == tag))
        return false;
    RemoveAt(*this, i);
    return true;
}

std::uint16_t GameplayTagContainer::BaselineCount(GameplayTagId tag) const
{
    const int i = LowerBound(Tags, Count, tag);
    return (i < Count && Tags[i] == tag) ? Baseline[i] : std::uint16_t{ 0 };
}

std::uint16_t GameplayTagContainer::ContributedCount(GameplayTagId tag) const
{
    const int i = LowerBound(Tags, Count, tag);
    return (i < Count && Tags[i] == tag) ? static_cast<std::uint16_t>(Counts[i] - Baseline[i]) : std::uint16_t{ 0 };
}

bool GameplayTagContainer::SetContribution(GameplayTagId tag, std::uint16_t stacks)
{
    const std::uint16_t held = ContributedCount(tag);
    if (held == stacks || !tag.IsValid())
        return false;
    if (held > stacks)
        (void)Withdraw(*this, tag, static_cast<std::uint16_t>(held - stacks), false);
    else
        (void)Add(*this, tag, static_cast<std::uint16_t>(stacks - held), false);
    return ContributedCount(tag) != held;
}

bool GameplayTagContainer::HasDescendantOf(const GameplayTagRegistry& registry,
                                           GameplayTagId ancestor) const
{
    for (int i = 0; i < Count; ++i)
        if (registry.IsDescendantOf(Tags[i], ancestor))
            return true;
    return false;
}
