#pragma once

#include <framework/gameplay_tags/GameplayTagId.h>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <unordered_set>

class GameplayTagRegistry;

class GameplayTagSet
{
public:
    using Storage = std::unordered_set<GameplayTagId>;
    using const_iterator = Storage::const_iterator;

    [[nodiscard]] bool AddExact(GameplayTagId tag);
    [[nodiscard]] bool RemoveExact(GameplayTagId tag);

    [[nodiscard]] bool HasExact(GameplayTagId tag) const;
    [[nodiscard]] bool HasDescendantOf(const GameplayTagRegistry& registry,
                                       GameplayTagId ancestor) const;
    [[nodiscard]] bool HasAnyExact(std::span<const GameplayTagId> tags) const;
    [[nodiscard]] bool HasAllExact(std::span<const GameplayTagId> tags) const;
    [[nodiscard]] bool HasNoneExact(std::span<const GameplayTagId> tags) const;
    [[nodiscard]] bool HasAnyExact(std::initializer_list<GameplayTagId> tags) const;
    [[nodiscard]] bool HasAllExact(std::initializer_list<GameplayTagId> tags) const;
    [[nodiscard]] bool HasNoneExact(std::initializer_list<GameplayTagId> tags) const;

    void Clear();

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] std::size_t Size() const;

    [[nodiscard]] const_iterator begin() const { return Tags.begin(); }
    [[nodiscard]] const_iterator end() const { return Tags.end(); }

private:
    Storage Tags;
};
