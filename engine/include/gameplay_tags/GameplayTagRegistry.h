#pragma once

#include <gameplay_tags/GameplayTagId.h>

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

    // A tag's identity between machines. Ids are registration order and mean
    // nothing to another process; the key is a hash of the name, the same
    // wherever the name is known. Zero is no tag.
    [[nodiscard]] static std::uint32_t WireKeyOf(std::string_view name);
    [[nodiscard]] std::uint32_t WireKey(GameplayTagId id) const;
    // The tag a wire key names here: invalid when no tag has it, or when two
    // registered names hash alike and the key cannot say which was meant.
    [[nodiscard]] GameplayTagId FindByWireKey(std::uint32_t key) const;

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
    // Invalid where two names share a key.
    std::unordered_map<std::uint32_t, GameplayTagId> IdsByWireKey;
};
