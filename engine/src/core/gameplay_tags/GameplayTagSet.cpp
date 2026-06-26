#include <core/gameplay_tags/GameplayTagSet.h>

#include <core/gameplay_tags/GameplayTagRegistry.h>

bool GameplayTagSet::AddExact(GameplayTagId tag)
{
    if (!tag.IsValid())
        return false;

    return Tags.insert(tag).second;
}

bool GameplayTagSet::RemoveExact(GameplayTagId tag)
{
    return Tags.erase(tag) > 0;
}

bool GameplayTagSet::HasExact(GameplayTagId tag) const
{
    return Tags.contains(tag);
}

bool GameplayTagSet::HasDescendantOf(const GameplayTagRegistry& registry,
                                     GameplayTagId ancestor) const
{
    for (GameplayTagId tag : Tags)
    {
        if (registry.IsDescendantOf(tag, ancestor))
            return true;
    }

    return false;
}

bool GameplayTagSet::HasAnyExact(std::span<const GameplayTagId> tags) const
{
    for (GameplayTagId tag : tags)
    {
        if (HasExact(tag))
            return true;
    }

    return false;
}

bool GameplayTagSet::HasAllExact(std::span<const GameplayTagId> tags) const
{
    for (GameplayTagId tag : tags)
    {
        if (!HasExact(tag))
            return false;
    }

    return true;
}

bool GameplayTagSet::HasNoneExact(std::span<const GameplayTagId> tags) const
{
    for (GameplayTagId tag : tags)
    {
        if (HasExact(tag))
            return false;
    }

    return true;
}

bool GameplayTagSet::HasAnyExact(std::initializer_list<GameplayTagId> tags) const
{
    return HasAnyExact(std::span<const GameplayTagId>(tags.begin(), tags.size()));
}

bool GameplayTagSet::HasAllExact(std::initializer_list<GameplayTagId> tags) const
{
    return HasAllExact(std::span<const GameplayTagId>(tags.begin(), tags.size()));
}

bool GameplayTagSet::HasNoneExact(std::initializer_list<GameplayTagId> tags) const
{
    return HasNoneExact(std::span<const GameplayTagId>(tags.begin(), tags.size()));
}

void GameplayTagSet::Clear()
{
    Tags.clear();
}

bool GameplayTagSet::Empty() const
{
    return Tags.empty();
}

std::size_t GameplayTagSet::Size() const
{
    return Tags.size();
}
