#include <gameplay_tags/GameplayTagQueryNames.h>

#include <gameplay_tags/GameplayTagRegistry.h>

GameplayTagQuery ResolveTagQuery(const GameplayTagQueryNames& names, const GameplayTagRegistry& tags,
                                 std::vector<std::string>& missing)
{
    GameplayTagQuery query;
    const auto add = [&](const std::vector<std::string>& listed, auto addClause) {
        for (const std::string& name : listed)
        {
            const GameplayTagId tag = tags.FindTag(name);
            if (!tag.IsValid())
            {
                missing.push_back(name);
                continue;
            }
            (query.*addClause)(tag, GameplayTagMatchMode::Hierarchical);
        }
    };
    add(names.All, &GameplayTagQuery::AddAll);
    add(names.Any, &GameplayTagQuery::AddAny);
    add(names.None, &GameplayTagQuery::AddNone);
    return query;
}
