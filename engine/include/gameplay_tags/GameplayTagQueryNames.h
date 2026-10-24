#pragma once

#include <gameplay_tags/GameplayTagQuery.h>

#include <string>
#include <vector>

class GameplayTagRegistry;

// A tag query as content spells it. Tag ids are registration-order values, so
// what persists is names; a World resolves them into a GameplayTagQuery.
struct GameplayTagQueryNames
{
    std::vector<std::string> All;
    std::vector<std::string> Any;
    std::vector<std::string> None;

    [[nodiscard]] bool Empty() const { return All.empty() && Any.empty() && None.empty(); }
    friend bool operator==(const GameplayTagQueryNames&, const GameplayTagQueryNames&) = default;
};

// Every name matches hierarchically. A name the registry does not hold is left
// out of the query and appended to `missing`.
[[nodiscard]] GameplayTagQuery ResolveTagQuery(const GameplayTagQueryNames& names, const GameplayTagRegistry& tags,
                                               std::vector<std::string>& missing);
