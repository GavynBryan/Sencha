#pragma once

#include <ecs/EntityId.h>
#include <navigation/NavAgent.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

enum class NavPlanClass : std::uint8_t
{
    // The agent has no route it may follow and stands still until one commits.
    Blocking,
    // The agent's route is still valid; a fresher one is wanted.
    Refresh,
};

struct NavPlanEntry
{
    EntityId Agent;
    NavRequestId Request;
    // Entries are taken in (OrderTick, Agent) order: the tick a blocking entry
    // was queued, or that plus the refresh deferral for a refresh.
    std::uint64_t OrderTick = 0;
    NavPlanClass Class = NavPlanClass::Blocking;
};

// Agents waiting for a route search, at most one entry each, in the order the
// follower plans them. See docs/navigation/runtime.md, "Agents".
class NavPlanQueue
{
public:
    // A new request replaces the agent's entry and takes the back of the
    // current tick. The same request keeps its place; a blocking trigger only
    // ever moves it earlier.
    void Queue(EntityId agent, NavRequestId request, NavPlanClass planClass, std::uint64_t tick,
               std::uint32_t refreshDeferTicks);
    void Remove(EntityId agent);
    [[nodiscard]] const NavPlanEntry* Find(EntityId agent) const;

    // Moves up to `budget` entries the predicate accepts, in order, into `out`.
    // Entries it declines keep their place.
    template <typename Accept>
    void Take(std::size_t budget, Accept&& accept, std::vector<NavPlanEntry>& out)
    {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < Entries_.size(); ++i)
        {
            if (out.size() < budget && accept(Entries_[i]))
                out.push_back(Entries_[i]);
            else
                Entries_[kept++] = Entries_[i];
        }
        Entries_.resize(kept);
    }

    [[nodiscard]] std::span<const NavPlanEntry> Entries() const { return Entries_; }
    [[nodiscard]] std::size_t Size() const { return Entries_.size(); }
    void Clear() { Entries_.clear(); }

private:
    void Insert(const NavPlanEntry& entry);

    std::vector<NavPlanEntry> Entries_;
};
