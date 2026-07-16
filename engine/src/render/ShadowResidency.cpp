#include <render/ShadowResidency.h>

#include <algorithm>

namespace
{
    constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= kFnvPrime;
        }
    }
}

std::uint64_t HashSpotShadowState(const SpotShadowView& view, std::uint32_t tileSize)
{
    std::uint64_t hash = kFnvOffset;
    HashBytes(hash, &view.ViewProjection, sizeof(view.ViewProjection));
    HashBytes(hash, &view.SamplingParams, sizeof(view.SamplingParams));
    HashBytes(hash, &tileSize, sizeof(tileSize));
    return hash;
}

void ShadowResidency::Update(std::span<const SpotShadowRequest> requests,
                             std::span<const ShadowCasterEvent> events,
                             const ShadowResidencyBudgets& budgets)
{
    ++FrameNumber;
    FrameGrants.clear();
    FrameViews.clear();

    IntakeEvents(events);
    MatchRequests(requests);
    EnforceSlotBudget(std::min(budgets.MaxSlots, kMaxSpotShadows));
    GrantFreeSlots(requests, std::min(budgets.MaxSlots, kMaxSpotShadows));
    ApplyHysteresisAndSteals(requests);
    ScheduleViews(requests, budgets);
    BuildGrants(requests);

    Stats = SpotShadowFrameStats{};
    Stats.RequestCount = static_cast<std::uint32_t>(requests.size());
    Stats.ViewsScheduled = static_cast<std::uint32_t>(FrameViews.size());
    for (const Slot& slot : Slots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX)
            continue;
        ++Stats.HeldRequests;
        if (slot.EverRendered && !slot.ScheduledThisFrame)
            ++Stats.CachedSlots;
    }
    Stats.DeniedRequests = Stats.RequestCount - Stats.HeldRequests;
}

void ShadowResidency::IntakeEvents(std::span<const ShadowCasterEvent> events)
{
    if (events.empty())
        return;

    for (Slot& slot : Slots)
    {
        if (!slot.Live || slot.Policy != ShadowUpdatePolicy::OnChange || slot.Invalid)
            continue;
        for (const ShadowCasterEvent& event : events)
        {
            if (slot.Volume.Intersects(event.Bounds))
            {
                MarkInvalid(slot);
                break;
            }
        }
    }
}

void ShadowResidency::MatchRequests(std::span<const SpotShadowRequest> requests)
{
    for (Slot& slot : Slots)
    {
        slot.RequestIndex = UINT32_MAX;
        slot.EffectiveScore = 0.0f;
        slot.ScheduledThisFrame = false;
    }

    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        const SpotShadowRequest& request = requests[requestIndex];
        for (Slot& slot : Slots)
        {
            if (!slot.Live || !(slot.Owner == request.Key)
                || slot.RequestIndex != UINT32_MAX)
            {
                continue;
            }

            slot.RequestIndex = requestIndex;
            slot.EffectiveScore = request.Score * kHolderScoreMultiplier;
            slot.Policy = request.Policy;
            slot.Volume = request.Bounds;

            if (slot.Allocation.Size != request.TileSize)
            {
                // The old tile's contents are unusable in a differently
                // placed rect, so a tier change re-acquires: the freed node
                // guarantees the downgrade chain terminates in a fit.
                Atlas.Free(slot.Allocation);
                slot.Allocation = AllocateWithDowngrade(request.TileSize);
                slot.EverRendered = false;
                slot.AcquiredFrame = FrameNumber;
                MarkInvalid(slot);
            }
            else if (slot.Policy == ShadowUpdatePolicy::OnChange
                     && slot.StateHash != request.StateHash)
            {
                MarkInvalid(slot);
            }
            break;
        }
    }
}

void ShadowResidency::EnforceSlotBudget(std::uint32_t maxSlots)
{
    while (LiveSlotCount() > maxSlots)
    {
        Slot* lowest = nullptr;
        for (Slot& slot : Slots)
        {
            if (!slot.Live)
                continue;
            if (lowest == nullptr || slot.EffectiveScore < lowest->EffectiveScore)
                lowest = &slot;
        }
        if (lowest == nullptr)
            return;
        ReleaseSlot(*lowest);
    }
}

void ShadowResidency::GrantFreeSlots(std::span<const SpotShadowRequest> requests,
                                     std::uint32_t maxSlots)
{
    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        if (LiveSlotCount() >= maxSlots)
            return;
        if (IsRequestGranted(requestIndex))
            continue;

        const ShadowAtlasAllocation allocation =
            AllocateWithDowngrade(requests[requestIndex].TileSize);
        if (!allocation.IsValid())
            return; // atlas exhausted; lower-scored requests cannot fit either

        for (Slot& slot : Slots)
        {
            if (slot.Live)
                continue;
            AcquireSlot(slot, requests[requestIndex], requestIndex, allocation);
            break;
        }
    }
}

void ShadowResidency::ApplyHysteresisAndSteals(std::span<const SpotShadowRequest> requests)
{
    // Contenders: requests still without a slot, strongest first (requests
    // already arrive score-descending). Holders: live slots, weakest first.
    std::vector<std::uint32_t> contenders;
    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        if (!IsRequestGranted(requestIndex))
            contenders.push_back(requestIndex);
    }

    std::vector<Slot*> holders;
    for (Slot& slot : Slots)
    {
        if (slot.Live)
            holders.push_back(&slot);
    }
    std::stable_sort(holders.begin(), holders.end(),
        [](const Slot* a, const Slot* b)
        {
            return a->EffectiveScore < b->EffectiveScore;
        });

    // Pair the strongest contender with the weakest holder and so on; a
    // holder's outscored run only grows while a contender strictly beats it,
    // and a steal happens only after the run reaches the threshold.
    for (std::size_t pair = 0; pair < holders.size(); ++pair)
    {
        Slot& holder = *holders[pair];
        const bool outscored = pair < contenders.size()
            && requests[contenders[pair]].Score > holder.EffectiveScore;
        if (!outscored)
        {
            holder.OutscoredFrames = 0;
            continue;
        }

        ++holder.OutscoredFrames;
        if (holder.OutscoredFrames < kStealOutscoredFrames)
            continue;

        const SpotShadowRequest& request = requests[contenders[pair]];
        Atlas.Free(holder.Allocation);
        const ShadowAtlasAllocation allocation =
            AllocateWithDowngrade(request.TileSize);
        AcquireSlot(holder, request, contenders[pair], allocation);
    }
}

void ShadowResidency::ScheduleViews(std::span<const SpotShadowRequest> requests,
                                    const ShadowResidencyBudgets& budgets)
{
    std::uint32_t budget = budgets.MaxViewsPerFrame == 0
        ? UINT32_MAX
        : budgets.MaxViewsPerFrame;

    const auto schedule = [&](Slot& slot)
    {
        const SpotShadowRequest& request = requests[slot.RequestIndex];
        FrameViews.push_back(SpotShadowViewJob{
            .SlotIndex = static_cast<std::uint32_t>(&slot - Slots),
            .Allocation = slot.Allocation,
            .ViewProjection = request.ViewProjection,
        });
        // The request's receiver-offset texel size was derived for the
        // reference tier; rescale it to the granted tile's logical interior
        // so a downgraded tile offsets by its real (coarser) texels.
        Vec4 samplingParams = request.SamplingParams;
        samplingParams.X *= static_cast<float>(kSpotShadowInnerExtent)
            / static_cast<float>(slot.Allocation.Size - 2u * kSpotShadowGuardTexels);
        slot.Rendered = SpotShadowView{
            .ViewProjection = request.ViewProjection,
            .AtlasScaleBias = ShadowAtlasAllocator::InsetScaleBias(slot.Allocation),
            .SamplingParams = samplingParams,
            .LightIndex = request.LightIndex,
        };
        slot.StateHash = request.StateHash;
        slot.EverRendered = true;
        slot.Invalid = false;
        slot.ScheduledThisFrame = true;
        slot.LastRenderedFrame = FrameNumber;
        --budget;
    };

    const auto schedulable = [&](const Slot& slot)
    {
        return slot.Live && slot.RequestIndex != UINT32_MAX
            && slot.Allocation.IsValid() && !slot.ScheduledThisFrame;
    };

    // Never-rendered slots, oldest acquisition first (slot index ties).
    std::vector<Slot*> neverRendered;
    for (Slot& slot : Slots)
    {
        if (schedulable(slot) && !slot.EverRendered)
            neverRendered.push_back(&slot);
    }
    std::stable_sort(neverRendered.begin(), neverRendered.end(),
        [](const Slot* a, const Slot* b)
        {
            return a->AcquiredFrame < b->AcquiredFrame;
        });
    for (Slot* slot : neverRendered)
    {
        if (budget == 0)
            break;
        schedule(*slot);
    }

    // Invalidated cached slots (non-EveryFrame), oldest invalidation first.
    std::vector<Slot*> invalidated;
    for (Slot& slot : Slots)
    {
        if (schedulable(slot) && slot.Invalid && slot.EverRendered
            && slot.Policy != ShadowUpdatePolicy::EveryFrame)
        {
            invalidated.push_back(&slot);
        }
    }
    std::stable_sort(invalidated.begin(), invalidated.end(),
        [](const Slot* a, const Slot* b)
        {
            return a->InvalidatedFrame < b->InvalidatedFrame;
        });

    // The reserved allotment keeps EveryFrame demand from starving the
    // oldest invalidations.
    std::size_t invalidatedServed = 0;
    const std::uint32_t reserve = std::min<std::uint32_t>(
        budgets.MinInvalidatedViewsPerFrame,
        static_cast<std::uint32_t>(invalidated.size()));
    while (invalidatedServed < reserve && budget > 0)
        schedule(*invalidated[invalidatedServed++]);

    // EveryFrame slots in stable slot order.
    for (Slot& slot : Slots)
    {
        if (budget == 0)
            break;
        if (schedulable(slot) && slot.EverRendered
            && slot.Policy == ShadowUpdatePolicy::EveryFrame)
        {
            schedule(slot);
        }
    }

    // Remaining budget drains the rest of the invalidated backlog.
    while (invalidatedServed < invalidated.size() && budget > 0)
        schedule(*invalidated[invalidatedServed++]);
}

void ShadowResidency::BuildGrants(std::span<const SpotShadowRequest> requests)
{
    for (const Slot& slot : Slots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX || !slot.EverRendered)
            continue;
        FrameGrants.push_back(SpotShadowGrant{
            .LightIndex = requests[slot.RequestIndex].LightIndex,
            .SlotIndex = static_cast<std::uint32_t>(&slot - Slots),
        });
    }
}

void ShadowResidency::AcquireSlot(Slot& slot, const SpotShadowRequest& request,
                                  std::uint32_t requestIndex,
                                  const ShadowAtlasAllocation& allocation)
{
    slot.Live = true;
    slot.Owner = request.Key;
    slot.Allocation = allocation;
    slot.Policy = request.Policy;
    slot.StateHash = request.StateHash;
    slot.Volume = request.Bounds;
    slot.Rendered = {};
    slot.EverRendered = false;
    slot.AcquiredFrame = FrameNumber;
    slot.OutscoredFrames = 0;
    slot.RequestIndex = requestIndex;
    slot.EffectiveScore = request.Score * kHolderScoreMultiplier;
    MarkInvalid(slot);
}

void ShadowResidency::ReleaseSlot(Slot& slot)
{
    Atlas.Free(slot.Allocation);
    slot = Slot{};
}

void ShadowResidency::MarkInvalid(Slot& slot)
{
    if (!slot.Invalid)
    {
        slot.Invalid = true;
        slot.InvalidatedFrame = FrameNumber;
    }
}

ShadowAtlasAllocation ShadowResidency::AllocateWithDowngrade(std::uint32_t tileSize)
{
    for (std::uint32_t size = tileSize; size >= kSpotShadowMinTileExtent; size >>= 1)
    {
        const ShadowAtlasAllocation allocation = Atlas.Allocate(size);
        if (allocation.IsValid())
            return allocation;
    }
    return {};
}

bool ShadowResidency::IsRequestGranted(std::uint32_t requestIndex) const
{
    for (const Slot& slot : Slots)
    {
        if (slot.Live && slot.RequestIndex == requestIndex)
            return true;
    }
    return false;
}

void ShadowResidency::ApplyGrants(RenderLightSet& lights) const
{
    for (const SpotShadowGrant& grant : FrameGrants)
        lights.Lights[grant.LightIndex].ShadowIndex = grant.SlotIndex;
    lights.SpotShadowCount = SlotHighWater();
    for (std::uint32_t slot = 0; slot < lights.SpotShadowCount; ++slot)
        lights.SpotShadows[slot] = Slots[slot].Rendered;
}

SpotShadowSlotInfo ShadowResidency::SlotInfo(std::uint32_t slot) const
{
    if (slot >= kMaxSpotShadows)
        return {};
    const Slot& state = Slots[slot];
    return SpotShadowSlotInfo{
        .Live = state.Live,
        .Owner = state.Owner,
        .Allocation = state.Allocation,
        .Policy = state.Policy,
        .EverRendered = state.EverRendered,
        .Invalid = state.Invalid,
        .FramesSinceAcquired = FrameNumber - state.AcquiredFrame,
        .FramesSinceRendered = state.EverRendered
            ? FrameNumber - state.LastRenderedFrame
            : 0u,
    };
}

std::uint32_t ShadowResidency::SlotHighWater() const
{
    std::uint32_t highWater = 0;
    for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
    {
        if (Slots[index].Live)
            highWater = index + 1;
    }
    return highWater;
}

bool ShadowResidency::HasOnChangeSlots() const
{
    for (const Slot& slot : Slots)
    {
        if (slot.Live && slot.Policy == ShadowUpdatePolicy::OnChange)
            return true;
    }
    return false;
}

std::uint32_t ShadowResidency::LiveSlotCount() const
{
    std::uint32_t count = 0;
    for (const Slot& slot : Slots)
        count += slot.Live ? 1u : 0u;
    return count;
}

void ShadowResidency::InvalidateAll()
{
    for (Slot& slot : Slots)
    {
        if (slot.Live && slot.EverRendered)
            MarkInvalid(slot);
    }
}

void ShadowResidency::MarkViewFailed(std::uint32_t slot)
{
    if (slot >= kMaxSpotShadows || !Slots[slot].Live)
        return;
    Slots[slot].EverRendered = false;
    MarkInvalid(Slots[slot]);
}

void ShadowResidency::Reset()
{
    for (Slot& slot : Slots)
        slot = Slot{};
    Atlas.Reset();
    FrameGrants.clear();
    FrameViews.clear();
    Stats = SpotShadowFrameStats{};
    FrameNumber = 0;
}
