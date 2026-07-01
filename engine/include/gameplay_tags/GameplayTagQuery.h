#pragma once

#include <gameplay_tags/GameplayTagId.h>

#include <vector>

class CountedGameplayTagSet;
class GameplayTagRegistry;
class GameplayTagSet;
struct GameplayTagContainer;

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
    // The ECS-storable per-entity container (ability activation requirements).
    [[nodiscard]] bool Matches(const GameplayTagContainer& tags,
                               const GameplayTagRegistry& registry) const;

private:
    std::vector<GameplayTagQueryClause> All;
    std::vector<GameplayTagQueryClause> Any;
    std::vector<GameplayTagQueryClause> None;
};
