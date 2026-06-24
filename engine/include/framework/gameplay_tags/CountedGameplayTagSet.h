#pragma once

#include <framework/gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class GameplayTagRegistry;

struct GameplayTagSource
{
    std::string Name;
    std::optional<std::string> Owner;
    std::optional<std::uint64_t> SourceId;
};

struct GameplayTagGrantHandle
{
    std::uint64_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    explicit operator bool() const { return IsValid(); }

    friend bool operator==(GameplayTagGrantHandle, GameplayTagGrantHandle) = default;
};

class CountedGameplayTagSet
{
public:
    [[nodiscard]] GameplayTagGrantHandle Grant(GameplayTagId tag, GameplayTagSource source);
    [[nodiscard]] bool Revoke(GameplayTagGrantHandle handle);

    [[nodiscard]] bool HasExact(GameplayTagId tag) const;
    [[nodiscard]] bool HasDescendantOf(const GameplayTagRegistry& registry,
                                       GameplayTagId ancestor) const;
    [[nodiscard]] std::uint32_t GetGrantCount(GameplayTagId tag) const;
    [[nodiscard]] std::vector<GameplayTagSource> GetSources(GameplayTagId tag) const;

    void Clear();

private:
    struct GrantRecord
    {
        GameplayTagId Tag;
        GameplayTagSource Source;
    };

    std::uint64_t NextGrantId = 1;
    std::unordered_map<std::uint64_t, GrantRecord> GrantsById;
    std::unordered_map<GameplayTagId, std::uint32_t> CountsByTag;
};
