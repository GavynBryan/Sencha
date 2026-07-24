#include <render/ShadowResidency.h>

#include <algorithm>
#include <bit>

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

std::uint64_t HashPointShadowState(const PointShadowView& view)
{
    std::uint64_t hash = kFnvOffset;
    HashBytes(hash, &view.PositionFar, sizeof(view.PositionFar));
    HashBytes(hash, &view.Params, sizeof(view.Params));
    return hash;
}

void ShadowResidency::Update(std::span<const SpotShadowRequest> requests,
                             std::span<const PointShadowRequest> pointRequests,
                             std::span<const ShadowCasterEvent> events,
                             const ShadowResidencyBudgets& budgets)
{
    ++FrameNumber;
    FrameGrants.clear();
    FramePointGrants.clear();
    FrameViews.clear();
    FramePointFaces.clear();

    IntakeEvents(events);
    MatchRequests(requests);
    MatchPointRequests(pointRequests);
    EnforceSlotBudget(std::min(budgets.MaxSlots, kMaxSpotShadows));
    EnforcePointSlotBudget(std::min(budgets.MaxPointSlots, kMaxPointShadows));
    GrantFreeSlots(requests, std::min(budgets.MaxSlots, kMaxSpotShadows));
    GrantFreePointSlots(pointRequests,
                        std::min(budgets.MaxPointSlots, kMaxPointShadows));
    ApplyHysteresisAndSteals(requests);
    ApplyPointHysteresisAndSteals(pointRequests);
    ScheduleViews(requests, pointRequests, budgets);
    BuildGrants(requests, pointRequests);

    Stats = ShadowFrameStats{};
    Stats.Spot.RequestCount = static_cast<std::uint32_t>(requests.size());
    Stats.Point.RequestCount = static_cast<std::uint32_t>(pointRequests.size());
    Stats.ViewsScheduled = static_cast<std::uint32_t>(
        FrameViews.size() + FramePointFaces.size());
    for (const Slot& slot : Slots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX)
            continue;
        ++Stats.Spot.HeldRequests;
        if (slot.EverRendered && !slot.ScheduledThisFrame)
            ++Stats.Spot.CachedSlots;
    }
    for (const PointSlot& slot : PointSlots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX)
            continue;
        ++Stats.Point.HeldRequests;
        if (slot.EverRendered && slot.PendingFaces == 0
            && !slot.ScheduledThisFrame)
            ++Stats.Point.CachedSlots;
    }
    Stats.Spot.DeniedRequests = Stats.Spot.RequestCount - Stats.Spot.HeldRequests;
    Stats.Point.DeniedRequests = Stats.Point.RequestCount - Stats.Point.HeldRequests;
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
    for (PointSlot& slot : PointSlots)
    {
        if (!slot.Live || slot.Policy != ShadowUpdatePolicy::OnChange || slot.Invalid)
            continue;
        for (const ShadowCasterEvent& event : events)
        {
            if (slot.Volume.Intersects(event.Bounds))
            {
                MarkPointInvalid(slot);
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

void ShadowResidency::MatchPointRequests(std::span<const PointShadowRequest> requests)
{
    for (PointSlot& slot : PointSlots)
    {
        slot.RequestIndex = UINT32_MAX;
        slot.EffectiveScore = 0.0f;
        slot.ScheduledThisFrame = false;
    }

    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        const PointShadowRequest& request = requests[requestIndex];
        for (PointSlot& slot : PointSlots)
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

            // Pending bits on a valid non-EveryFrame slot are leftover
            // freshness rotation from an EveryFrame era, not invalidity.
            if (slot.Policy != ShadowUpdatePolicy::EveryFrame
                && !slot.Invalid && slot.EverRendered)
            {
                slot.PendingFaces = 0;
            }
            if (slot.Policy != ShadowUpdatePolicy::Static
                && slot.StateHash != request.StateHash)
            {
                MarkPointInvalid(slot);
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

void ShadowResidency::EnforcePointSlotBudget(std::uint32_t maxSlots)
{
    while (LivePointSlotCount() > maxSlots)
    {
        PointSlot* lowest = nullptr;
        for (PointSlot& slot : PointSlots)
        {
            if (!slot.Live)
                continue;
            if (lowest == nullptr || slot.EffectiveScore < lowest->EffectiveScore)
                lowest = &slot;
        }
        if (lowest == nullptr)
            return;
        *lowest = PointSlot{};
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

void ShadowResidency::GrantFreePointSlots(
    std::span<const PointShadowRequest> requests, std::uint32_t maxSlots)
{
    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        if (LivePointSlotCount() >= maxSlots)
            return;
        if (IsPointRequestGranted(requestIndex))
            continue;

        for (PointSlot& slot : PointSlots)
        {
            if (slot.Live)
                continue;
            AcquirePointSlot(slot, requests[requestIndex], requestIndex);
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

void ShadowResidency::ApplyPointHysteresisAndSteals(
    std::span<const PointShadowRequest> requests)
{
    std::vector<std::uint32_t> contenders;
    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        if (!IsPointRequestGranted(requestIndex))
            contenders.push_back(requestIndex);
    }

    std::vector<PointSlot*> holders;
    for (PointSlot& slot : PointSlots)
    {
        if (slot.Live)
            holders.push_back(&slot);
    }
    std::stable_sort(holders.begin(), holders.end(),
        [](const PointSlot* a, const PointSlot* b)
        {
            return a->EffectiveScore < b->EffectiveScore;
        });

    for (std::size_t pair = 0; pair < holders.size(); ++pair)
    {
        PointSlot& holder = *holders[pair];
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

        AcquirePointSlot(holder, requests[contenders[pair]], contenders[pair]);
    }
}

void ShadowResidency::ScheduleViews(std::span<const SpotShadowRequest> requests,
                                    std::span<const PointShadowRequest> pointRequests,
                                    const ShadowResidencyBudgets& budgets)
{
    std::uint32_t budget = budgets.MaxViewsPerFrame == 0
        ? UINT32_MAX
        : budgets.MaxViewsPerFrame;

    const auto scheduleSpot = [&](Slot& slot)
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

    const auto schedulePointFace = [&](PointSlot& slot, std::uint32_t face)
    {
        const PointShadowRequest& request = pointRequests[slot.RequestIndex];
        const Vec<3> position(request.View.PositionFar.X,
                              request.View.PositionFar.Y,
                              request.View.PositionFar.Z);
        FramePointFaces.push_back(PointShadowFaceJob{
            .SlotIndex = static_cast<std::uint32_t>(&slot - PointSlots),
            .Face = face,
            .ViewProjection = MakePointShadowFaceViewProjection(
                position, face, request.View.Params.X, request.View.PositionFar.W),
        });
        slot.Rendered = request.View;
        slot.Rendered.LightIndex = request.LightIndex;
        slot.StateHash = request.StateHash;
        slot.PendingFaces &= ~(1u << face);
        slot.ScheduledThisFrame = true;
        slot.LastRenderedFrame = FrameNumber;
        if (slot.PendingFaces == 0)
        {
            slot.EverRendered = true;
            slot.Invalid = false;
        }
        --budget;
    };

    const auto spotSchedulable = [](const Slot& slot)
    {
        return slot.Live && slot.RequestIndex != UINT32_MAX
            && slot.Allocation.IsValid() && !slot.ScheduledThisFrame;
    };
    const auto pointSchedulable = [](const PointSlot& slot)
    {
        return slot.Live && slot.RequestIndex != UINT32_MAX;
    };

    // References into either pool, ordered by a frame stamp with spot slots
    // winning ties, then slot index, so the cross-pool order is total.
    struct SlotRef
    {
        std::uint32_t Frame = 0;
        bool IsPoint = false;
        std::uint32_t Index = 0;
    };
    const auto refOrder = [](const SlotRef& a, const SlotRef& b)
    {
        if (a.Frame != b.Frame)
            return a.Frame < b.Frame;
        if (a.IsPoint != b.IsPoint)
            return !a.IsPoint;
        return a.Index < b.Index;
    };

    std::vector<SlotRef> neverRendered;
    for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
    {
        if (spotSchedulable(Slots[index]) && !Slots[index].EverRendered)
            neverRendered.push_back({ Slots[index].AcquiredFrame, false, index });
    }
    for (std::uint32_t index = 0; index < kMaxPointShadows; ++index)
    {
        if (pointSchedulable(PointSlots[index]) && !PointSlots[index].EverRendered)
            neverRendered.push_back({ PointSlots[index].AcquiredFrame, true, index });
    }
    std::sort(neverRendered.begin(), neverRendered.end(), refOrder);

    // Invalidated cached slots (non-EveryFrame), oldest invalidation first.
    std::vector<SlotRef> invalidated;
    for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
    {
        const Slot& slot = Slots[index];
        if (spotSchedulable(slot) && slot.Invalid && slot.EverRendered
            && slot.Policy != ShadowUpdatePolicy::EveryFrame)
        {
            invalidated.push_back({ slot.InvalidatedFrame, false, index });
        }
    }
    for (std::uint32_t index = 0; index < kMaxPointShadows; ++index)
    {
        const PointSlot& slot = PointSlots[index];
        if (pointSchedulable(slot) && slot.Invalid && slot.EverRendered
            && slot.Policy != ShadowUpdatePolicy::EveryFrame)
        {
            invalidated.push_back({ slot.InvalidatedFrame, true, index });
        }
    }
    std::sort(invalidated.begin(), invalidated.end(), refOrder);

    // The reserved allotment keeps EveryFrame demand and never-rendered
    // point-face bursts from starving the oldest invalidations.
    std::uint32_t invalidatedDemand = 0;
    for (const SlotRef& ref : invalidated)
    {
        invalidatedDemand += ref.IsPoint
            ? std::popcount(PointSlots[ref.Index].PendingFaces)
            : 1u;
    }
    const std::uint32_t reserve = std::min(
        budgets.MinInvalidatedViewsPerFrame, invalidatedDemand);

    // Never-rendered slots first. Spot tiles may use the full budget; point
    // face bursts stop short of the reserved invalidation views.
    for (const SlotRef& ref : neverRendered)
    {
        if (budget == 0)
            break;
        if (!ref.IsPoint)
        {
            scheduleSpot(Slots[ref.Index]);
            continue;
        }
        PointSlot& slot = PointSlots[ref.Index];
        for (std::uint32_t face = 0;
             face < kPointShadowFaceCount && budget > reserve;
             ++face)
        {
            if ((slot.PendingFaces & (1u << face)) != 0)
                schedulePointFace(slot, face);
        }
    }

    // Serve the reserve, oldest invalidation first; a point slot re-renders
    // one face per reserved view and keeps the rest pending.
    std::uint32_t reserveRemaining = reserve;
    for (const SlotRef& ref : invalidated)
    {
        if (reserveRemaining == 0 || budget == 0)
            break;
        if (!ref.IsPoint)
        {
            scheduleSpot(Slots[ref.Index]);
            --reserveRemaining;
            continue;
        }
        PointSlot& slot = PointSlots[ref.Index];
        for (std::uint32_t face = 0;
             face < kPointShadowFaceCount && reserveRemaining > 0 && budget > 0;
             ++face)
        {
            if ((slot.PendingFaces & (1u << face)) != 0)
            {
                schedulePointFace(slot, face);
                --reserveRemaining;
            }
        }
    }

    // EveryFrame slots in stable slot order, spot pool then point pool. A
    // point slot refills its face rotation only once the previous rotation
    // finished, so a budget clamp still cycles coverage over all six faces.
    for (Slot& slot : Slots)
    {
        if (budget == 0)
            break;
        if (spotSchedulable(slot) && slot.EverRendered
            && slot.Policy == ShadowUpdatePolicy::EveryFrame)
        {
            scheduleSpot(slot);
        }
    }
    for (PointSlot& slot : PointSlots)
    {
        if (budget == 0)
            break;
        if (!pointSchedulable(slot) || !slot.EverRendered
            || slot.Policy != ShadowUpdatePolicy::EveryFrame)
        {
            continue;
        }
        if (slot.PendingFaces == 0)
            slot.PendingFaces = kAllPointFacesMask;
        for (std::uint32_t face = 0;
             face < kPointShadowFaceCount && budget > 0;
             ++face)
        {
            if ((slot.PendingFaces & (1u << face)) != 0)
                schedulePointFace(slot, face);
        }
    }

    // Remaining budget drains the rest of the invalidated backlog; pending
    // masks and the spot schedulable check skip work the reserve already did.
    for (const SlotRef& ref : invalidated)
    {
        if (budget == 0)
            break;
        if (!ref.IsPoint)
        {
            if (spotSchedulable(Slots[ref.Index]))
                scheduleSpot(Slots[ref.Index]);
            continue;
        }
        PointSlot& slot = PointSlots[ref.Index];
        for (std::uint32_t face = 0;
             face < kPointShadowFaceCount && budget > 0;
             ++face)
        {
            if ((slot.PendingFaces & (1u << face)) != 0)
                schedulePointFace(slot, face);
        }
    }
}

void ShadowResidency::BuildGrants(std::span<const SpotShadowRequest> requests,
                                  std::span<const PointShadowRequest> pointRequests)
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
    for (const PointSlot& slot : PointSlots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX
            || !slot.EverRendered || slot.PendingFaces != 0)
            continue;
        FramePointGrants.push_back(PointShadowGrant{
            .LightIndex = pointRequests[slot.RequestIndex].LightIndex,
            .SlotIndex = static_cast<std::uint32_t>(&slot - PointSlots),
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

void ShadowResidency::AcquirePointSlot(PointSlot& slot,
                                       const PointShadowRequest& request,
                                       std::uint32_t requestIndex)
{
    slot.Live = true;
    slot.Owner = request.Key;
    slot.Policy = request.Policy;
    slot.StateHash = request.StateHash;
    slot.Volume = request.Bounds;
    slot.Rendered = {};
    slot.PendingFaces = kAllPointFacesMask;
    slot.EverRendered = false;
    slot.Invalid = false;
    slot.AcquiredFrame = FrameNumber;
    slot.OutscoredFrames = 0;
    slot.RequestIndex = requestIndex;
    slot.EffectiveScore = request.Score * kHolderScoreMultiplier;
    MarkPointInvalid(slot);
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

void ShadowResidency::MarkPointInvalid(PointSlot& slot)
{
    if (!slot.Invalid)
    {
        slot.Invalid = true;
        slot.InvalidatedFrame = FrameNumber;
    }
    // State changes dirty every face, including any a previous invalidation
    // already re-rendered.
    slot.PendingFaces = kAllPointFacesMask;
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

bool ShadowResidency::IsPointRequestGranted(std::uint32_t requestIndex) const
{
    for (const PointSlot& slot : PointSlots)
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

    for (const PointShadowGrant& grant : FramePointGrants)
        lights.Lights[grant.LightIndex].ShadowIndex = grant.SlotIndex;
    lights.PointShadowCount = PointSlotHighWater();
    for (std::uint32_t slot = 0; slot < lights.PointShadowCount; ++slot)
        lights.PointShadows[slot] = PointSlots[slot].Rendered;
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

PointShadowSlotInfo ShadowResidency::PointSlotInfo(std::uint32_t slot) const
{
    if (slot >= kMaxPointShadows)
        return {};
    const PointSlot& state = PointSlots[slot];
    return PointShadowSlotInfo{
        .Live = state.Live,
        .Owner = state.Owner,
        .Policy = state.Policy,
        .EverRendered = state.EverRendered,
        .Invalid = state.Invalid,
        .PendingFaces = state.PendingFaces,
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

std::uint32_t ShadowResidency::PointSlotHighWater() const
{
    std::uint32_t highWater = 0;
    for (std::uint32_t index = 0; index < kMaxPointShadows; ++index)
    {
        if (PointSlots[index].Live)
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
    for (const PointSlot& slot : PointSlots)
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

std::uint32_t ShadowResidency::LivePointSlotCount() const
{
    std::uint32_t count = 0;
    for (const PointSlot& slot : PointSlots)
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
    for (PointSlot& slot : PointSlots)
    {
        if (slot.Live && slot.EverRendered)
            MarkPointInvalid(slot);
    }
}

void ShadowResidency::MarkViewFailed(std::uint32_t slot)
{
    if (slot >= kMaxSpotShadows || !Slots[slot].Live)
        return;
    Slots[slot].EverRendered = false;
    MarkInvalid(Slots[slot]);
    std::erase_if(FrameGrants,
        [slot](const SpotShadowGrant& grant) { return grant.SlotIndex == slot; });
}

void ShadowResidency::MarkPointFaceFailed(std::uint32_t slot, std::uint32_t face)
{
    if (slot >= kMaxPointShadows || face >= kPointShadowFaceCount
        || !PointSlots[slot].Live)
    {
        return;
    }
    PointSlot& state = PointSlots[slot];
    state.PendingFaces |= 1u << face;
    if (!state.Invalid)
    {
        state.Invalid = true;
        state.InvalidatedFrame = FrameNumber;
    }
    std::erase_if(FramePointGrants,
        [slot](const PointShadowGrant& grant) { return grant.SlotIndex == slot; });
}

void ShadowResidency::Reset()
{
    for (Slot& slot : Slots)
        slot = Slot{};
    for (PointSlot& slot : PointSlots)
        slot = PointSlot{};
    Atlas.Reset();
    FrameGrants.clear();
    FramePointGrants.clear();
    FrameViews.clear();
    FramePointFaces.clear();
    Stats = ShadowFrameStats{};
    FrameNumber = 0;
}
