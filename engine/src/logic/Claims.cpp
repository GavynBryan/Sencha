#include <logic/Claims.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <cmath>

void ClaimBoard::Prune(const World& world)
{
    std::erase_if(Places, [&](const Place& place) { return !world.IsAlive(place.Holder.Entity); });
}

bool ClaimBoard::Take(const World& world, GameplayTagId group, GameplayTagId claim, const LeaseOwner& holder,
                      std::uint32_t capacity)
{
    if (!holder.IsSet() || !group.IsValid() || !claim.IsValid())
        return false;
    Prune(world);
    std::uint32_t held = 0;
    for (const Place& place : Places)
    {
        if (place.Group != group || place.Claim != claim)
            continue;
        if (place.Holder == holder)
            return true;
        ++held;
    }
    if (held >= capacity)
        return false;
    Places.push_back({ group, claim, holder });
    return true;
}

void ClaimBoard::Release(GameplayTagId group, GameplayTagId claim, const LeaseOwner& holder)
{
    std::erase_if(Places, [&](const Place& place) {
        return place.Group == group && place.Claim == claim && place.Holder == holder;
    });
}

void ClaimBoard::ReleaseAll(const LeaseOwner& holder)
{
    std::erase_if(Places, [&](const Place& place) { return place.Holder == holder; });
}

std::uint32_t ClaimBoard::Count(const World& world, GameplayTagId group, GameplayTagId claim) const
{
    return static_cast<std::uint32_t>(std::ranges::count_if(Places, [&](const Place& place) {
        return place.Group == group && place.Claim == claim && world.IsAlive(place.Holder.Entity);
    }));
}

bool ClaimBoard::Holds(const World& world, GameplayTagId group, GameplayTagId claim, EntityId entity) const
{
    return world.IsAlive(entity) && std::ranges::any_of(Places, [&](const Place& place) {
        return place.Group == group && place.Claim == claim && place.Holder.Entity == entity;
    });
}

std::uint32_t CountGroupMembers(const World& world, GameplayTagId group, const GameplayTagRegistry& tags)
{
    if (!group.IsValid() || !world.IsRegistered<GameplayTagContainer>())
        return 0;
    GameplayTagQuery query;
    query.AddAll(group, GameplayTagMatchMode::Hierarchical);
    std::uint32_t members = 0;
    world.ForEachComponent<GameplayTagContainer>([&](EntityId, const GameplayTagContainer& held) {
        if (query.Matches(held, tags))
            ++members;
    });
    return members;
}

std::uint32_t ClaimCapacity(std::uint32_t count, float share, std::uint32_t members)
{
    const float shared = std::isfinite(share) && share > 0.0f ? std::floor(share * static_cast<float>(members)) : 0.0f;
    return std::max(count, static_cast<std::uint32_t>(shared));
}
