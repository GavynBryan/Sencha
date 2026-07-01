#include <gameplay_tags/GameplayTagQuery.h>

#include <gameplay_tags/CountedGameplayTagSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSet.h>

namespace
{
    template <typename TSet>
    bool MatchesClause(const TSet& tags,
                       const GameplayTagRegistry& registry,
                       const GameplayTagQueryClause& clause)
    {
        if (clause.Match == GameplayTagMatchMode::Exact)
            return tags.HasExact(clause.Tag);

        return tags.HasDescendantOf(registry, clause.Tag);
    }

    template <typename TSet>
    bool MatchesQuery(const TSet& tags,
                      const GameplayTagRegistry& registry,
                      const std::vector<GameplayTagQueryClause>& all,
                      const std::vector<GameplayTagQueryClause>& any,
                      const std::vector<GameplayTagQueryClause>& none)
    {
        for (const GameplayTagQueryClause& clause : all)
        {
            if (!MatchesClause(tags, registry, clause))
                return false;
        }

        if (!any.empty())
        {
            bool matchedAny = false;
            for (const GameplayTagQueryClause& clause : any)
            {
                if (MatchesClause(tags, registry, clause))
                {
                    matchedAny = true;
                    break;
                }
            }

            if (!matchedAny)
                return false;
        }

        for (const GameplayTagQueryClause& clause : none)
        {
            if (MatchesClause(tags, registry, clause))
                return false;
        }

        return true;
    }
}

GameplayTagQuery& GameplayTagQuery::AddAll(GameplayTagId tag, GameplayTagMatchMode match)
{
    All.push_back(GameplayTagQueryClause{ tag, match });
    return *this;
}

GameplayTagQuery& GameplayTagQuery::AddAny(GameplayTagId tag, GameplayTagMatchMode match)
{
    Any.push_back(GameplayTagQueryClause{ tag, match });
    return *this;
}

GameplayTagQuery& GameplayTagQuery::AddNone(GameplayTagId tag, GameplayTagMatchMode match)
{
    None.push_back(GameplayTagQueryClause{ tag, match });
    return *this;
}

bool GameplayTagQuery::Matches(const GameplayTagSet& tags,
                               const GameplayTagRegistry& registry) const
{
    return MatchesQuery(tags, registry, All, Any, None);
}

bool GameplayTagQuery::Matches(const CountedGameplayTagSet& tags,
                               const GameplayTagRegistry& registry) const
{
    return MatchesQuery(tags, registry, All, Any, None);
}

bool GameplayTagQuery::Matches(const GameplayTagContainer& tags,
                               const GameplayTagRegistry& registry) const
{
    return MatchesQuery(tags, registry, All, Any, None);
}
