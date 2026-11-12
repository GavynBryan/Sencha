#pragma once

#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <cstdint>
#include <vector>

class GameplayTagRegistry;
class World;

// Places a group's members can hold in a claim with a capacity: a role only so
// many may take, a token only so many may spend, a signal. A holder is a
// LeaseOwner, so an ability that holds a claim through claims.hold lets go when
// it ends, and a holder whose entity is gone holds nothing.
// docs/gameplay/claims.md.
class ClaimBoard
{
public:
    // Takes a place in `claim` within `group` for `holder`: true when it holds
    // one now, already did, or one of `capacity` was free.
    bool Take(const World& world, GameplayTagId group, GameplayTagId claim, const LeaseOwner& holder,
              std::uint32_t capacity);
    void Release(GameplayTagId group, GameplayTagId claim, const LeaseOwner& holder);
    void ReleaseAll(const LeaseOwner& holder);

    // Places held by living holders.
    [[nodiscard]] std::uint32_t Count(const World& world, GameplayTagId group, GameplayTagId claim) const;
    // Whether any of `entity`'s leases holds a place.
    [[nodiscard]] bool Holds(const World& world, GameplayTagId group, GameplayTagId claim, EntityId entity) const;

private:
    struct Place
    {
        GameplayTagId Group;
        GameplayTagId Claim;
        LeaseOwner Holder;
    };

    void Prune(const World& world);

    std::vector<Place> Places;
};

// Live entities whose tags hold `group` or a tag beneath it.
[[nodiscard]] std::uint32_t CountGroupMembers(const World& world, GameplayTagId group,
                                              const GameplayTagRegistry& tags);

// Those same members within `radius` of `centre`, in identity order.
void CollectGroupMembersWithin(const World& world, GameplayTagId group, const GameplayTagRegistry& tags,
                               const Vec3d& centre, float radius, std::vector<EntityId>& out);

// A claim's capacity in a group of `members`: `count`, or `share` of the
// members rounded down if that is more.
[[nodiscard]] std::uint32_t ClaimCapacity(std::uint32_t count, float share, std::uint32_t members);
