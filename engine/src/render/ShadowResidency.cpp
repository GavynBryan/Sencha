#include <render/ShadowResidency.h>

#include <core/hash/Fnv1a.h>

#include <algorithm>

namespace
{
// A spot tile renders as one sub-view: bit 0.
constexpr std::uint32_t kSpotAllSubViewsMask = 0x1u;

// What each pool's schedule hook needs, threaded as the descriptor's self
// (the hooks are capture-free function pointers). Budget accounting stays
// with the scheduler; the hooks only emit the job and stamp the slot.
struct SpotScheduleContext
{
    std::span<const SpotShadowRequest> Requests;
    const ShadowSlotState* SlotsBase = nullptr;
    const ShadowAtlasAllocation* Allocations = nullptr;
    SpotShadowView* Rendered = nullptr;
    std::vector<SpotShadowViewJob>* Views = nullptr;
    std::uint32_t FrameNumber = 0;
};

bool SpotEligible(const void* selfErased, const ShadowSlotState& slot)
{
    const auto* self = static_cast<const SpotScheduleContext*>(selfErased);
    return self->Allocations[&slot - self->SlotsBase].IsValid();
}

void ScheduleSpotSubView(void* selfErased, ShadowSlotState& slot, std::uint32_t)
{
    auto* self = static_cast<SpotScheduleContext*>(selfErased);
    const std::uint32_t index =
        static_cast<std::uint32_t>(&slot - self->SlotsBase);
    const SpotShadowRequest& request = self->Requests[slot.RequestIndex];
    const ShadowAtlasAllocation& allocation = self->Allocations[index];
    self->Views->push_back(SpotShadowViewJob{
        .SlotIndex = index,
        .Allocation = allocation,
        .ViewProjection = request.ViewProjection,
    });
    // The request's receiver-offset texel size was derived for the
    // reference tier; rescale it to the granted tile's logical interior
    // so a downgraded tile offsets by its real (coarser) texels.
    Vec4 samplingParams = request.SamplingParams;
    samplingParams.X *= static_cast<float>(kSpotShadowInnerExtent)
        / static_cast<float>(allocation.Size - 2u * kSpotShadowGuardTexels);
    self->Rendered[index] = SpotShadowView{
        .ViewProjection = request.ViewProjection,
        .AtlasScaleBias = ShadowAtlasAllocator::InsetScaleBias(allocation),
        .SamplingParams = samplingParams,
        .LightIndex = request.LightIndex,
    };
    slot.StateHash = request.StateHash;
    slot.PendingSubViews &= ~kSpotAllSubViewsMask;
    slot.EverRendered = true;
    slot.Invalid = false;
    slot.ScheduledThisFrame = true;
    slot.LastRenderedFrame = self->FrameNumber;
}

struct PointScheduleContext
{
    std::span<const PointShadowRequest> Requests;
    const ShadowSlotState* SlotsBase = nullptr;
    PointShadowView* Rendered = nullptr;
    std::vector<PointShadowFaceJob>* Faces = nullptr;
    std::uint32_t FrameNumber = 0;
};

bool PointEligible(const void*, const ShadowSlotState&)
{
    return true;
}

void SchedulePointSubView(void* selfErased, ShadowSlotState& slot,
                          std::uint32_t face)
{
    auto* self = static_cast<PointScheduleContext*>(selfErased);
    const std::uint32_t index =
        static_cast<std::uint32_t>(&slot - self->SlotsBase);
    const PointShadowRequest& request = self->Requests[slot.RequestIndex];
    const Vec<3> position(request.View.PositionFar.X,
                          request.View.PositionFar.Y,
                          request.View.PositionFar.Z);
    self->Faces->push_back(PointShadowFaceJob{
        .SlotIndex = index,
        .Face = face,
        .ViewProjection = MakePointShadowFaceViewProjection(
            position, face, request.View.Params.X, request.View.PositionFar.W),
    });
    self->Rendered[index] = request.View;
    self->Rendered[index].LightIndex = request.LightIndex;
    slot.StateHash = request.StateHash;
    slot.PendingSubViews &= ~(1u << face);
    slot.ScheduledThisFrame = true;
    slot.LastRenderedFrame = self->FrameNumber;
    if (slot.PendingSubViews == 0)
    {
        slot.EverRendered = true;
        slot.Invalid = false;
    }
}
} // namespace

std::uint64_t HashSpotShadowState(const SpotShadowView& view, std::uint32_t tileSize)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aValue(hash, view.ViewProjection);
    HashFnv1aValue(hash, view.SamplingParams);
    HashFnv1aValue(hash, tileSize);
    return hash;
}

std::uint64_t HashPointShadowState(const PointShadowView& view)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aValue(hash, view.PositionFar);
    HashFnv1aValue(hash, view.Params);
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
    for (const ShadowSlotState& slot : Slots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX)
            continue;
        ++Stats.Spot.HeldRequests;
        if (slot.EverRendered && !slot.ScheduledThisFrame)
            ++Stats.Spot.CachedSlots;
    }
    for (const ShadowSlotState& slot : PointSlots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX)
            continue;
        ++Stats.Point.HeldRequests;
        if (slot.EverRendered && slot.PendingSubViews == 0
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

    IntakeShadowCasterEvents(std::span<ShadowSlotState>(Slots), events,
                     [this](ShadowSlotState& slot) { MarkInvalid(slot); });
    IntakeShadowCasterEvents(std::span<ShadowSlotState>(PointSlots), events,
                     [this](ShadowSlotState& slot) { MarkPointInvalid(slot); });
}

void ShadowResidency::MatchRequests(std::span<const SpotShadowRequest> requests)
{
    ResetShadowFrameTransients(std::span<ShadowSlotState>(Slots));

    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        const SpotShadowRequest& request = requests[requestIndex];
        for (ShadowSlotState& slot : Slots)
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

            ShadowAtlasAllocation& allocation =
                SpotAllocations[&slot - Slots];
            if (allocation.Size != request.TileSize)
            {
                // The old tile's contents are unusable in a differently
                // placed rect, so a tier change re-acquires: the freed node
                // guarantees the downgrade chain terminates in a fit.
                Atlas.Free(allocation);
                allocation = AllocateWithDowngrade(request.TileSize);
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
    ResetShadowFrameTransients(std::span<ShadowSlotState>(PointSlots));

    for (std::uint32_t requestIndex = 0;
         requestIndex < static_cast<std::uint32_t>(requests.size());
         ++requestIndex)
    {
        const PointShadowRequest& request = requests[requestIndex];
        for (ShadowSlotState& slot : PointSlots)
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
                slot.PendingSubViews = 0;
            }
            // Hash-change invalidation is OnChange's mechanism, same as the
            // spot pool: an EveryFrame slot's staleness is already bounded
            // by its own re-render rotation, and resetting that rotation on
            // every change made a moving light re-render its first faces
            // forever while the rest starved. Static ignores drift by
            // contract.
            if (slot.Policy == ShadowUpdatePolicy::OnChange
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
    EnforceShadowSlotBudget(std::span<ShadowSlotState>(Slots), maxSlots,
                    [this](ShadowSlotState& slot) { ReleaseSlot(slot); });
}

void ShadowResidency::EnforcePointSlotBudget(std::uint32_t maxSlots)
{
    EnforceShadowSlotBudget(std::span<ShadowSlotState>(PointSlots), maxSlots,
                    [this](ShadowSlotState& slot)
                    {
                        PointRendered[&slot - PointSlots] = {};
                        slot = ShadowSlotState{};
                    });
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

        for (ShadowSlotState& slot : Slots)
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

        for (ShadowSlotState& slot : PointSlots)
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
    ApplyShadowSlotHysteresis(
        std::span<ShadowSlotState>(Slots), requests, kStealOutscoredFrames,
        [&](ShadowSlotState& holder, std::uint32_t requestIndex)
        {
            const SpotShadowRequest& request = requests[requestIndex];
            Atlas.Free(SpotAllocations[&holder - Slots]);
            const ShadowAtlasAllocation allocation =
                AllocateWithDowngrade(request.TileSize);
            AcquireSlot(holder, request, requestIndex, allocation);
        });
}

void ShadowResidency::ApplyPointHysteresisAndSteals(
    std::span<const PointShadowRequest> requests)
{
    ApplyShadowSlotHysteresis(
        std::span<ShadowSlotState>(PointSlots), requests, kStealOutscoredFrames,
        [&](ShadowSlotState& holder, std::uint32_t requestIndex)
        {
            AcquirePointSlot(holder, requests[requestIndex], requestIndex);
        });
}

void ShadowResidency::ScheduleViews(std::span<const SpotShadowRequest> requests,
                                    std::span<const PointShadowRequest> pointRequests,
                                    const ShadowResidencyBudgets& budgets)
{
    SpotScheduleContext spotContext{
        .Requests = requests,
        .SlotsBase = Slots,
        .Allocations = SpotAllocations,
        .Rendered = SpotRendered,
        .Views = &FrameViews,
        .FrameNumber = FrameNumber,
    };
    PointScheduleContext pointContext{
        .Requests = pointRequests,
        .SlotsBase = PointSlots,
        .Rendered = PointRendered,
        .Faces = &FramePointFaces,
        .FrameNumber = FrameNumber,
    };
    // Pool ordinal is the cross-pool tie-break (lower wins), so the spot
    // pool comes first to keep tiles ahead of faces on equal frame stamps.
    ShadowSlotPool pools[] = {
        ShadowSlotPool{
            .Slots = Slots,
            .AllSubViewsMask = kSpotAllSubViewsMask,
            .BurstRespectsReserve = false,
            .EveryFrameSkipsScheduled = true,
            .Eligible = &SpotEligible,
            .ScheduleSubView = &ScheduleSpotSubView,
            .Self = &spotContext,
        },
        ShadowSlotPool{
            .Slots = PointSlots,
            .AllSubViewsMask = kAllPointFacesMask,
            .BurstRespectsReserve = true,
            .Eligible = &PointEligible,
            .ScheduleSubView = &SchedulePointSubView,
            .Self = &pointContext,
        },
    };
    ScheduleShadowSubViews(pools, budgets.MaxViewsPerFrame,
                           budgets.MinInvalidatedViewsPerFrame);
}

void ShadowResidency::BuildGrants(std::span<const SpotShadowRequest> requests,
                                  std::span<const PointShadowRequest> pointRequests)
{
    for (const ShadowSlotState& slot : Slots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX || !slot.EverRendered)
            continue;
        FrameGrants.push_back(SpotShadowGrant{
            .LightIndex = requests[slot.RequestIndex].LightIndex,
            .SlotIndex = static_cast<std::uint32_t>(&slot - Slots),
        });
    }
    // EverRendered alone gates the grant: a cube must never be sampled
    // before its first full rotation (garbage faces), but once every face
    // has rendered, pending re-render work -- a clamped EveryFrame
    // rotation, a budget-split invalidation drain, a failed face -- serves
    // the cached faces rather than dropping the shadow. Face age is
    // bounded by the schedule queues; a stale face beats a flicker.
    for (const ShadowSlotState& slot : PointSlots)
    {
        if (!slot.Live || slot.RequestIndex == UINT32_MAX || !slot.EverRendered)
            continue;
        FramePointGrants.push_back(PointShadowGrant{
            .LightIndex = pointRequests[slot.RequestIndex].LightIndex,
            .SlotIndex = static_cast<std::uint32_t>(&slot - PointSlots),
        });
    }
}

void ShadowResidency::AcquireSlot(ShadowSlotState& slot, const SpotShadowRequest& request,
                                  std::uint32_t requestIndex,
                                  const ShadowAtlasAllocation& allocation)
{
    slot.Live = true;
    slot.Owner = request.Key;
    SpotAllocations[&slot - Slots] = allocation;
    slot.Policy = request.Policy;
    slot.StateHash = request.StateHash;
    slot.Volume = request.Bounds;
    SpotRendered[&slot - Slots] = {};
    slot.EverRendered = false;
    // A stolen slot may arrive still flagged from its previous owner; the
    // new owner's invalidation stamp must be its own. (The stale stamp was
    // proven unobservable -- the never-rendered path re-queues the slot
    // before anything reads it -- so this is state hygiene, matching
    // AcquirePointSlot, not a behavior change.)
    slot.Invalid = false;
    slot.AcquiredFrame = FrameNumber;
    slot.OutscoredFrames = 0;
    slot.RequestIndex = requestIndex;
    slot.EffectiveScore = request.Score * kHolderScoreMultiplier;
    MarkInvalid(slot);
}

void ShadowResidency::AcquirePointSlot(ShadowSlotState& slot,
                                       const PointShadowRequest& request,
                                       std::uint32_t requestIndex)
{
    slot.Live = true;
    slot.Owner = request.Key;
    slot.Policy = request.Policy;
    slot.StateHash = request.StateHash;
    slot.Volume = request.Bounds;
    PointRendered[&slot - PointSlots] = {};
    slot.PendingSubViews = kAllPointFacesMask;
    slot.EverRendered = false;
    slot.Invalid = false;
    slot.AcquiredFrame = FrameNumber;
    slot.OutscoredFrames = 0;
    slot.RequestIndex = requestIndex;
    slot.EffectiveScore = request.Score * kHolderScoreMultiplier;
    MarkPointInvalid(slot);
}

void ShadowResidency::ReleaseSlot(ShadowSlotState& slot)
{
    Atlas.Free(SpotAllocations[&slot - Slots]);
    SpotAllocations[&slot - Slots] = {};
    SpotRendered[&slot - Slots] = {};
    slot = ShadowSlotState{};
}

void ShadowResidency::MarkInvalid(ShadowSlotState& slot)
{
    if (!slot.Invalid)
    {
        slot.Invalid = true;
        slot.InvalidatedFrame = FrameNumber;
    }
    // A spot tile re-renders whole, so any invalidation dirties its one
    // sub-view -- the same rule MarkPointInvalid applies to all six faces.
    slot.PendingSubViews = kSpotAllSubViewsMask;
}

void ShadowResidency::MarkPointInvalid(ShadowSlotState& slot)
{
    if (!slot.Invalid)
    {
        slot.Invalid = true;
        slot.InvalidatedFrame = FrameNumber;
    }
    // State changes dirty every face, including any a previous invalidation
    // already re-rendered.
    slot.PendingSubViews = kAllPointFacesMask;
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
    return IsShadowRequestGranted(std::span<const ShadowSlotState>(Slots), requestIndex);
}

bool ShadowResidency::IsPointRequestGranted(std::uint32_t requestIndex) const
{
    return IsShadowRequestGranted(std::span<const ShadowSlotState>(PointSlots), requestIndex);
}

void ShadowResidency::ApplyGrants(RenderLightSet& lights) const
{
    for (const SpotShadowGrant& grant : FrameGrants)
        lights.Lights[grant.LightIndex].ShadowIndex = grant.SlotIndex;
    lights.SpotShadowCount = SlotHighWater();
    for (std::uint32_t slot = 0; slot < lights.SpotShadowCount; ++slot)
        lights.SpotShadows[slot] = SpotRendered[slot];

    for (const PointShadowGrant& grant : FramePointGrants)
        lights.Lights[grant.LightIndex].ShadowIndex = grant.SlotIndex;
    lights.PointShadowCount = PointSlotHighWater();
    for (std::uint32_t slot = 0; slot < lights.PointShadowCount; ++slot)
        lights.PointShadows[slot] = PointRendered[slot];
}

SpotShadowSlotInfo ShadowResidency::SlotInfo(std::uint32_t slot) const
{
    if (slot >= kMaxSpotShadows)
        return {};
    const ShadowSlotState& state = Slots[slot];
    return SpotShadowSlotInfo{
        .Live = state.Live,
        .Owner = state.Owner,
        .Allocation = SpotAllocations[slot],
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
    const ShadowSlotState& state = PointSlots[slot];
    return PointShadowSlotInfo{
        .Live = state.Live,
        .Owner = state.Owner,
        .Policy = state.Policy,
        .EverRendered = state.EverRendered,
        .Invalid = state.Invalid,
        .PendingFaces = state.PendingSubViews,
        .FramesSinceAcquired = FrameNumber - state.AcquiredFrame,
        .FramesSinceRendered = state.EverRendered
            ? FrameNumber - state.LastRenderedFrame
            : 0u,
    };
}

std::uint32_t ShadowResidency::SlotHighWater() const
{
    return ShadowSlotHighWater(std::span<const ShadowSlotState>(Slots));
}

std::uint32_t ShadowResidency::PointSlotHighWater() const
{
    return ShadowSlotHighWater(std::span<const ShadowSlotState>(PointSlots));
}

bool ShadowResidency::HasOnChangeSlots() const
{
    return HasOnChangeShadowSlots(std::span<const ShadowSlotState>(Slots))
        || HasOnChangeShadowSlots(std::span<const ShadowSlotState>(PointSlots));
}

std::uint32_t ShadowResidency::LiveSlotCount() const
{
    return LiveShadowSlotCount(std::span<const ShadowSlotState>(Slots));
}

std::uint32_t ShadowResidency::LivePointSlotCount() const
{
    return LiveShadowSlotCount(std::span<const ShadowSlotState>(PointSlots));
}

void ShadowResidency::ClearFrameSchedule()
{
    FrameGrants.clear();
    FramePointGrants.clear();
    FrameViews.clear();
    FramePointFaces.clear();
}

void ShadowResidency::InvalidateAll()
{
    for (ShadowSlotState& slot : Slots)
    {
        if (slot.Live && slot.EverRendered)
            MarkInvalid(slot);
    }
    for (ShadowSlotState& slot : PointSlots)
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
    ShadowSlotState& state = PointSlots[slot];
    state.PendingSubViews |= 1u << face;
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
    for (ShadowSlotState& slot : Slots)
        slot = ShadowSlotState{};
    for (ShadowSlotState& slot : PointSlots)
        slot = ShadowSlotState{};
    for (ShadowAtlasAllocation& allocation : SpotAllocations)
        allocation = {};
    for (SpotShadowView& rendered : SpotRendered)
        rendered = {};
    for (PointShadowView& rendered : PointRendered)
        rendered = {};
    Atlas.Reset();
    FrameGrants.clear();
    FramePointGrants.clear();
    FrameViews.clear();
    FramePointFaces.clear();
    Stats = ShadowFrameStats{};
    FrameNumber = 0;
}
