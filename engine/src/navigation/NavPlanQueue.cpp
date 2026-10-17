#include <navigation/NavPlanQueue.h>

#include <algorithm>

namespace
{
    bool PlansBefore(const NavPlanEntry& a, const NavPlanEntry& b)
    {
        return a.OrderTick != b.OrderTick ? a.OrderTick < b.OrderTick : a.Agent < b.Agent;
    }
}

void NavPlanQueue::Queue(EntityId agent, NavRequestId request, NavPlanClass planClass, std::uint64_t tick,
                         std::uint32_t refreshDeferTicks)
{
    const std::uint64_t order = planClass == NavPlanClass::Refresh ? tick + refreshDeferTicks : tick;
    const auto existing = std::ranges::find(Entries_, agent, &NavPlanEntry::Agent);
    if (existing != Entries_.end() && existing->Request == request)
    {
        if (planClass == NavPlanClass::Refresh || existing->Class == NavPlanClass::Blocking)
            return;
        NavPlanEntry raised = *existing;
        raised.Class = NavPlanClass::Blocking;
        raised.OrderTick = std::min(raised.OrderTick, order);
        Entries_.erase(existing);
        Insert(raised);
        return;
    }
    if (existing != Entries_.end())
        Entries_.erase(existing);
    Insert(NavPlanEntry{ agent, request, order, planClass });
}

void NavPlanQueue::Remove(EntityId agent)
{
    std::erase_if(Entries_, [agent](const NavPlanEntry& entry) { return entry.Agent == agent; });
}

const NavPlanEntry* NavPlanQueue::Find(EntityId agent) const
{
    const auto found = std::ranges::find(Entries_, agent, &NavPlanEntry::Agent);
    return found != Entries_.end() ? &*found : nullptr;
}

void NavPlanQueue::Insert(const NavPlanEntry& entry)
{
    Entries_.insert(std::upper_bound(Entries_.begin(), Entries_.end(), entry, PlansBefore), entry);
}
