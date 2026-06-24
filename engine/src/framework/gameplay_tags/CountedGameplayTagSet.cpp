#include <framework/gameplay_tags/CountedGameplayTagSet.h>

#include <framework/gameplay_tags/GameplayTagRegistry.h>

#include <utility>

GameplayTagGrantHandle CountedGameplayTagSet::Grant(GameplayTagId tag, GameplayTagSource source)
{
    if (!tag.IsValid())
        return {};

    const GameplayTagGrantHandle handle{ NextGrantId++ };
    GrantsById.emplace(handle.Value, GrantRecord{ tag, std::move(source) });
    ++CountsByTag[tag];
    return handle;
}

bool CountedGameplayTagSet::Revoke(GameplayTagGrantHandle handle)
{
    if (!handle.IsValid())
        return false;

    auto grantIt = GrantsById.find(handle.Value);
    if (grantIt == GrantsById.end())
        return false;

    auto countIt = CountsByTag.find(grantIt->second.Tag);
    if (countIt != CountsByTag.end())
    {
        if (countIt->second > 1)
            --countIt->second;
        else
            CountsByTag.erase(countIt);
    }

    GrantsById.erase(grantIt);
    return true;
}

bool CountedGameplayTagSet::HasExact(GameplayTagId tag) const
{
    return GetGrantCount(tag) > 0;
}

bool CountedGameplayTagSet::HasDescendantOf(const GameplayTagRegistry& registry,
                                            GameplayTagId ancestor) const
{
    for (const auto& [tag, count] : CountsByTag)
    {
        if (count > 0 && registry.IsDescendantOf(tag, ancestor))
            return true;
    }

    return false;
}

std::uint32_t CountedGameplayTagSet::GetGrantCount(GameplayTagId tag) const
{
    auto it = CountsByTag.find(tag);
    return it == CountsByTag.end() ? 0 : it->second;
}

std::vector<GameplayTagSource> CountedGameplayTagSet::GetSources(GameplayTagId tag) const
{
    std::vector<GameplayTagSource> sources;
    for (const auto& [_, grant] : GrantsById)
    {
        if (grant.Tag == tag)
            sources.push_back(grant.Source);
    }

    return sources;
}

void CountedGameplayTagSet::Clear()
{
    GrantsById.clear();
    CountsByTag.clear();
}
