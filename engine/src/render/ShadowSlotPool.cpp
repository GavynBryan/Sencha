#include <render/ShadowSlotPool.h>

#include <bit>

namespace
{
// A reference into one pool's slot array; PoolOrdinal replaces the old
// per-kind flag so ordering and dispatch are both data.
struct SlotRef
{
    std::uint32_t Frame = 0;
    std::uint32_t PoolOrdinal = 0;
    std::uint32_t Index = 0;
};

// Total cross-pool order: frame stamp, then pool ordinal (lower wins, which
// keeps spot tiles ahead of point faces on ties), then slot index.
bool RefOrder(const SlotRef& a, const SlotRef& b)
{
    if (a.Frame != b.Frame)
        return a.Frame < b.Frame;
    if (a.PoolOrdinal != b.PoolOrdinal)
        return a.PoolOrdinal < b.PoolOrdinal;
    return a.Index < b.Index;
}

bool Schedulable(const ShadowSlotPool& pool, const ShadowSlotState& slot)
{
    return slot.Live && slot.RequestIndex != UINT32_MAX
        && pool.Eligible(pool.Self, slot);
}

// Every schedulable slot matching `pred`, stamped by `stamp` and sorted into
// the cross-pool order. The stamp is what the sort ages by: acquisition frame
// for first renders, invalidation frame for re-renders.
template <typename Pred>
std::vector<SlotRef> GatherRefs(std::span<ShadowSlotPool> pools,
                                std::uint32_t ShadowSlotState::*stamp,
                                Pred pred)
{
    std::vector<SlotRef> refs;
    for (std::uint32_t ordinal = 0;
         ordinal < static_cast<std::uint32_t>(pools.size()); ++ordinal)
    {
        const ShadowSlotPool& pool = pools[ordinal];
        for (std::uint32_t index = 0;
             index < static_cast<std::uint32_t>(pool.Slots.size()); ++index)
        {
            const ShadowSlotState& slot = pool.Slots[index];
            if (Schedulable(pool, slot) && pred(slot))
                refs.push_back({ slot.*stamp, ordinal, index });
        }
    }
    std::sort(refs.begin(), refs.end(), RefOrder);
    return refs;
}

// Schedules `slot`'s pending sub-views in ascending bit order while the
// budget stays above `floor` and, when given, `*secondary` has views left
// (decremented per scheduled view). This is the one drain loop every
// scheduling phase shares; the phases differ only in what bounds it.
void DrainSlot(const ShadowSlotPool& pool, ShadowSlotState& slot,
               const std::uint32_t& budget, std::uint32_t floor,
               std::uint32_t* secondary, const auto& schedule)
{
    const std::uint32_t subViews = std::bit_width(pool.AllSubViewsMask);
    for (std::uint32_t subView = 0; subView < subViews; ++subView)
    {
        if (budget <= floor || (secondary != nullptr && *secondary == 0))
            break;
        if ((slot.PendingSubViews & (1u << subView)) == 0)
            continue;
        schedule(pool, slot, subView);
        if (secondary != nullptr)
            --(*secondary);
    }
}
} // namespace

void ScheduleShadowSubViews(std::span<ShadowSlotPool> pools,
                            std::uint32_t maxViewsPerFrame,
                            std::uint32_t minInvalidatedViewsPerFrame)
{
    std::uint32_t budget = maxViewsPerFrame == 0 ? UINT32_MAX : maxViewsPerFrame;

    const auto schedule = [&](const ShadowSlotPool& pool, ShadowSlotState& slot,
                              std::uint32_t subView)
    {
        pool.ScheduleSubView(pool.Self, slot, subView);
        --budget;
    };

    const std::vector<SlotRef> neverRendered = GatherRefs(
        pools, &ShadowSlotState::AcquiredFrame,
        [](const ShadowSlotState& slot) { return !slot.EverRendered; });

    // Invalidated cached slots (non-EveryFrame), oldest invalidation first.
    const std::vector<SlotRef> invalidated = GatherRefs(
        pools, &ShadowSlotState::InvalidatedFrame,
        [](const ShadowSlotState& slot)
        {
            return slot.Invalid && slot.EverRendered
                && slot.Policy != ShadowUpdatePolicy::EveryFrame;
        });

    // The reserved allotment keeps EveryFrame demand and never-rendered
    // sub-view bursts from starving the oldest invalidations. An invalid
    // slot's demand is exactly its pending mask.
    std::uint32_t invalidatedDemand = 0;
    for (const SlotRef& ref : invalidated)
        invalidatedDemand +=
            std::popcount(pools[ref.PoolOrdinal].Slots[ref.Index].PendingSubViews);
    const std::uint32_t reserve =
        std::min(minInvalidatedViewsPerFrame, invalidatedDemand);

    // Never-rendered slots first; a pool whose bursts respect the reserve
    // stops short of the views the oldest invalidations are owed.
    for (const SlotRef& ref : neverRendered)
    {
        if (budget == 0)
            break;
        const ShadowSlotPool& pool = pools[ref.PoolOrdinal];
        DrainSlot(pool, pool.Slots[ref.Index], budget,
                  pool.BurstRespectsReserve ? reserve : 0u, nullptr, schedule);
    }

    // Serve the reserve, oldest invalidation first; a slot re-renders one
    // pending sub-view per reserved view and keeps the rest pending.
    std::uint32_t reserveRemaining = reserve;
    for (const SlotRef& ref : invalidated)
    {
        if (reserveRemaining == 0 || budget == 0)
            break;
        const ShadowSlotPool& pool = pools[ref.PoolOrdinal];
        DrainSlot(pool, pool.Slots[ref.Index], budget, 0u, &reserveRemaining,
                  schedule);
    }

    // EveryFrame slots in stable slot order, pool by pool. A slot refills
    // its sub-view rotation only once the previous rotation finished, so a
    // budget clamp still cycles coverage over every sub-view.
    for (const ShadowSlotPool& pool : pools)
    {
        for (ShadowSlotState& slot : pool.Slots)
        {
            if (budget == 0)
                return;
            if (!Schedulable(pool, slot) || !slot.EverRendered
                || slot.Policy != ShadowUpdatePolicy::EveryFrame
                || (pool.EveryFrameSkipsScheduled && slot.ScheduledThisFrame))
            {
                continue;
            }
            if (slot.PendingSubViews == 0)
                slot.PendingSubViews = pool.AllSubViewsMask;
            DrainSlot(pool, slot, budget, 0u, nullptr, schedule);
        }
    }

    // Remaining budget drains the rest of the invalidated backlog; pending
    // masks skip the sub-views the reserve already rendered.
    for (const SlotRef& ref : invalidated)
    {
        if (budget == 0)
            break;
        const ShadowSlotPool& pool = pools[ref.PoolOrdinal];
        DrainSlot(pool, pool.Slots[ref.Index], budget, 0u, nullptr, schedule);
    }
}
