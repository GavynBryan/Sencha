#pragma once

#include <core/gameplay_tags/GameplayTagId.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct GameplayTagError
{
    std::string Message;
    std::size_t Position = 0;
};

class GameplayTagRegistry
{
public:
    GameplayTagRegistry();

    // Registers a canonical dot-separated tag name and auto-creates any missing
    // parents. Duplicate registration of the same name returns the existing id.
    [[nodiscard]] std::optional<GameplayTagId> RegisterTag(std::string_view name,
                                                           GameplayTagError* error = nullptr);

    [[nodiscard]] GameplayTagId FindTag(std::string_view name) const;
    [[nodiscard]] std::string_view GetName(GameplayTagId id) const;
    [[nodiscard]] GameplayTagId GetParent(GameplayTagId id) const;

    // Inclusive subtree test: a tag is considered a descendant of itself.
    [[nodiscard]] bool IsDescendantOf(GameplayTagId child, GameplayTagId ancestor) const;
    [[nodiscard]] std::vector<GameplayTagId> GetChildren(GameplayTagId id) const;
    [[nodiscard]] std::size_t Size() const;

private:
    struct TagRecord
    {
        std::string Name;
        GameplayTagId Parent;
        std::vector<GameplayTagId> Children;
    };

    [[nodiscard]] bool IsKnown(GameplayTagId id) const;
    [[nodiscard]] std::optional<GameplayTagId> EnsureSegmentPath(std::string_view canonicalName,
                                                                 GameplayTagError* error);

    std::vector<TagRecord> Tags;
    std::unordered_map<std::string, GameplayTagId> IdsByName;
};
