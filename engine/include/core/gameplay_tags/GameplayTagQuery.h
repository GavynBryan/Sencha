#pragma once

#include <core/gameplay_tags/GameplayTagId.h>

#include <vector>

class CountedGameplayTagSet;
class GameplayTagRegistry;
class GameplayTagSet;

enum class GameplayTagMatchMode
{
    Exact,
    Hierarchical,
};

struct GameplayTagQueryClause
{
    GameplayTagId Tag;
    GameplayTagMatchMode Match = GameplayTagMatchMode::Exact;
};

class GameplayTagQuery
{
public:
    GameplayTagQuery& AddAll(GameplayTagId tag,
                             GameplayTagMatchMode match = GameplayTagMatchMode::Exact);
    GameplayTagQuery& AddAny(GameplayTagId tag,
                             GameplayTagMatchMode match = GameplayTagMatchMode::Exact);
    GameplayTagQuery& AddNone(GameplayTagId tag,
                              GameplayTagMatchMode match = GameplayTagMatchMode::Exact);

    [[nodiscard]] bool Matches(const GameplayTagSet& tags,
                               const GameplayTagRegistry& registry) const;
    [[nodiscard]] bool Matches(const CountedGameplayTagSet& tags,
                               const GameplayTagRegistry& registry) const;

private:
    std::vector<GameplayTagQueryClause> All;
    std::vector<GameplayTagQueryClause> Any;
    std::vector<GameplayTagQueryClause> None;
};
