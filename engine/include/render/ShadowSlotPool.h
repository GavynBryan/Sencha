#pragma once

#include <render/RenderEntityKey.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidencyTypes.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// ShadowSlotPool
//
// The pool-neutral half of shadow slot residency. A slot pool's one real
// variation axis is how many sub-views a slot renders as -- a spot tile is
// one, a point cube is six face bits -- so a slot's state is one struct, a
// pool is a span of them plus that mask and its pool-specific hooks, and
// every mechanical rule (matching transients, event intake, budgets,
// hysteresis, the cross-pool sub-view schedule) is written once over that
// shape. ShadowResidency composes the pools and owns everything typed: the
// atlas allocator, the rendered GPU records, and the invalidation policies
// that still differ per pool.
//
// Adding a pool means one more ShadowSlotPool entry with its own mask and
// hooks, not another copy of the scheduling.
//=============================================================================

struct ShadowSlotState
{
    bool Live = false;
    RenderEntityKey Owner;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    std::uint64_t StateHash = 0;
    Sphere Volume;
    // Sub-views still owed a render, one bit each; the pool's full mask is
    // what a fresh or fully dirtied slot carries.
    std::uint32_t PendingSubViews = 0;
    bool EverRendered = false;
    bool Invalid = false;
    std::uint32_t AcquiredFrame = 0;
    std::uint32_t LastRenderedFrame = 0;
    std::uint32_t InvalidatedFrame = 0;
    std::uint32_t OutscoredFrames = 0;

    // Per-frame transients, rebuilt each Update.
    std::uint32_t RequestIndex = UINT32_MAX;
    float EffectiveScore = 0.0f;
    bool ScheduledThisFrame = false;
};

// One pool as the cross-pool scheduler sees it. The hooks are capture-free
// function pointers with an explicit self (the FrameComposition record
// shape), so building the descriptors allocates nothing per frame.
struct ShadowSlotPool
{
    std::span<ShadowSlotState> Slots;
    // Every sub-view bit a slot of this pool renders as.
    std::uint32_t AllSubViewsMask = 0;
    // Whether a never-rendered burst must stop short of the views reserved
    // for the oldest invalidations: point face bursts do, spot tiles may
    // use the full budget (recorded scheduling rule).
    bool BurstRespectsReserve = false;
    // Whether the EveryFrame phase skips a slot already scheduled this
    // frame. The spot pool does; the point pool does not, so a fresh
    // EveryFrame cube re-renders its rotation right after its first-frame
    // burst -- pinned pre-refactor behavior, carried as data rather than
    // silently unified.
    bool EveryFrameSkipsScheduled = false;
    // Pool-specific eligibility beyond Live + a matched request (a spot slot
    // also needs a valid atlas allocation).
    bool (*Eligible)(const void* self, const ShadowSlotState& slot) = nullptr;
    // Emits one sub-view's render job and stamps the slot's rendered state.
    // The scheduler owns the budget; the hook must not count views.
    void (*ScheduleSubView)(void* self, ShadowSlotState& slot,
                            std::uint32_t subView) = nullptr;
    void* Self = nullptr;
};

// The bounded-fair sub-view schedule across every pool, in pool-ordinal
// order for ties (lower ordinal wins, which keeps spot tiles ahead of point
// faces): never-rendered slots first (oldest acquisition first), a reserved
// allotment for the oldest invalidated cached slots, then EveryFrame work
// in stable slot order per pool, then the remaining invalidated backlog.
void ScheduleShadowSubViews(std::span<ShadowSlotPool> pools,
                            std::uint32_t maxViewsPerFrame,
                            std::uint32_t minInvalidatedViewsPerFrame);

inline void ResetShadowFrameTransients(std::span<ShadowSlotState> slots)
{
    for (ShadowSlotState& slot : slots)
    {
        slot.RequestIndex = UINT32_MAX;
        slot.EffectiveScore = 0.0f;
        slot.ScheduledThisFrame = false;
    }
}

// Marks every valid OnChange slot an intersecting caster event dirties; how
// a slot invalidates stays with the caller.
template <typename MarkFn>
void IntakeShadowCasterEvents(std::span<ShadowSlotState> slots,
                              std::span<const ShadowCasterEvent> events,
                              MarkFn&& mark)
{
    for (ShadowSlotState& slot : slots)
    {
        if (!slot.Live || slot.Policy != ShadowUpdatePolicy::OnChange || slot.Invalid)
            continue;
        for (const ShadowCasterEvent& event : events)
        {
            if (slot.Volume.Intersects(event.Bounds))
            {
                mark(slot);
                break;
            }
        }
    }
}

[[nodiscard]] inline std::uint32_t LiveShadowSlotCount(
    std::span<const ShadowSlotState> slots)
{
    std::uint32_t count = 0;
    for (const ShadowSlotState& slot : slots)
        count += slot.Live ? 1u : 0u;
    return count;
}

[[nodiscard]] inline std::uint32_t ShadowSlotHighWater(
    std::span<const ShadowSlotState> slots)
{
    std::uint32_t highWater = 0;
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(slots.size()); ++index)
    {
        if (slots[index].Live)
            highWater = index + 1;
    }
    return highWater;
}

[[nodiscard]] inline bool IsShadowRequestGranted(
    std::span<const ShadowSlotState> slots, std::uint32_t requestIndex)
{
    for (const ShadowSlotState& slot : slots)
    {
        if (slot.Live && slot.RequestIndex == requestIndex)
            return true;
    }
    return false;
}

[[nodiscard]] inline bool HasOnChangeShadowSlots(
    std::span<const ShadowSlotState> slots)
{
    for (const ShadowSlotState& slot : slots)
    {
        if (slot.Live && slot.Policy == ShadowUpdatePolicy::OnChange)
            return true;
    }
    return false;
}

// Evicts the weakest live slot until the pool fits its budget; releasing is
// the caller's, since it may free typed resources beside the state.
template <typename ReleaseFn>
void EnforceShadowSlotBudget(std::span<ShadowSlotState> slots,
                             std::uint32_t maxSlots, ReleaseFn&& release)
{
    while (LiveShadowSlotCount(slots) > maxSlots)
    {
        ShadowSlotState* lowest = nullptr;
        for (ShadowSlotState& slot : slots)
        {
            if (!slot.Live)
                continue;
            if (lowest == nullptr || slot.EffectiveScore < lowest->EffectiveScore)
                lowest = &slot;
        }
        if (lowest == nullptr)
            return;
        release(*lowest);
    }
}

// Pairs the strongest still-ungranted contender with the weakest holder and
// so on; a holder's outscored run only grows while a contender strictly
// beats it, and a steal happens only after the run reaches the threshold.
//
// Hysteresis exists to stop ownership flickering between two lights that
// are both asking for a slot. A holder that produced no request this frame
// is not competing: its owner was culled, or destroyed outright when its
// zone detached. Making a waiting light outscore an absent holder for a
// fixed run only delays a grant that nothing contests. Uncontended slots
// still retain their cached content, so a brief absence stays free.
template <typename RequestT, typename StealFn>
void ApplyShadowSlotHysteresis(std::span<ShadowSlotState> slots,
                               std::span<const RequestT> requests,
                               std::uint32_t stealOutscoredFrames,
                               StealFn&& steal)
{
    std::vector<std::uint32_t> contenders;
    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        if (!IsShadowRequestGranted(slots, requestIndex))
            contenders.push_back(requestIndex);
    }

    std::vector<ShadowSlotState*> holders;
    for (ShadowSlotState& slot : slots)
    {
        if (slot.Live)
            holders.push_back(&slot);
    }
    std::stable_sort(holders.begin(), holders.end(),
        [](const ShadowSlotState* a, const ShadowSlotState* b)
        {
            return a->EffectiveScore < b->EffectiveScore;
        });

    for (std::size_t pair = 0; pair < holders.size(); ++pair)
    {
        ShadowSlotState& holder = *holders[pair];
        const bool outscored = pair < contenders.size()
            && requests[contenders[pair]].Score > holder.EffectiveScore;
        if (!outscored)
        {
            holder.OutscoredFrames = 0;
            continue;
        }

        ++holder.OutscoredFrames;
        if (holder.RequestIndex != UINT32_MAX
            && holder.OutscoredFrames < stealOutscoredFrames)
        {
            continue;
        }

        steal(holder, contenders[pair]);
    }
}
