#pragma once

#include <render/RenderLight.h>
#include <render/ShadowAtlasAllocator.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidencyTypes.h>
#include <render/ShadowSlotPool.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// ShadowResidency
//
// The shadow slot arbiter: owns which light holds which atlas tile or cube
// slot, when cached content re-renders, and which depth views run this
// frame. Deterministic and CPU-only: identical request/event sequences
// produce identical slot assignment, atlas placement, and view schedules.
//
// Two pools with separate slot budgets share one per-frame view budget.
// Spot slots hold atlas tiles and re-render as a single view. Point slots
// hold one cube each; every face is an individual view for the clamp, and a
// slot tracks the faces still pending so a budget-split render continues
// where it left off. A point light stays ungranted until all six faces have
// rendered at least once, and thereafter holds its grant through rotations
// and re-renders: face age is bounded by the schedule, and a slightly stale
// face beats a flickering shadow.
//
// Scoring and hysteresis: a slot holder's score gets a fixed multiplier, and
// a contender must outscore a holder for a fixed run of consecutive frames
// before the slot is stolen, so equal-importance lights cannot flicker
// ownership. Spot tier requests are served through downgrade before denial;
// point faces have one fixed size.
//
// Update policies: EveryFrame slots re-render whenever scheduled, OnChange
// slots re-render on state-hash changes or intersecting caster events, and
// Static slots re-render only through InvalidateAll (the explicit console
// path). Scheduling follows the bounded-fair order across both pools:
// never-rendered slots first (oldest acquisition first; a never-rendered
// point light bursts contiguous faces only with budget the reserved
// invalidation views do not need), a reserved allotment for the oldest
// invalidated cached slots, then EveryFrame work in stable slot order
// (spot slots, then point slots), then remaining invalidated work.
//
// Uniform-facing slot records hold what was last rendered, not what was last
// requested, so cached content is always sampled with the state it was drawn
// with.
//=============================================================================
class ShadowResidency
{
public:
    static constexpr float kHolderScoreMultiplier = 1.25f;
    static constexpr std::uint32_t kStealOutscoredFrames = 30;
    static constexpr std::uint32_t kAllPointFacesMask =
        (1u << kPointShadowFaceCount) - 1u;

    void Update(std::span<const SpotShadowRequest> requests,
                std::span<const PointShadowRequest> pointRequests,
                std::span<const ShadowCasterEvent> events,
                const ShadowResidencyBudgets& budgets);
    void Update(std::span<const SpotShadowRequest> requests,
                std::span<const ShadowCasterEvent> events,
                const ShadowResidencyBudgets& budgets)
    {
        Update(requests, {}, events, budgets);
    }

    [[nodiscard]] std::span<const SpotShadowGrant> Grants() const { return FrameGrants; }
    [[nodiscard]] std::span<const PointShadowGrant> PointGrants() const
    {
        return FramePointGrants;
    }
    [[nodiscard]] std::span<const SpotShadowViewJob> ScheduledViews() const { return FrameViews; }
    [[nodiscard]] std::span<const PointShadowFaceJob> ScheduledPointFaces() const
    {
        return FramePointFaces;
    }

    // Writes the frame's grants onto the packed light set: shadow indices for
    // granted lights, the slot high waters, and the last-rendered slot records.
    void ApplyGrants(RenderLightSet& lights) const;

    // The GPU record for one slot as last rendered; meaningful only for
    // slots referenced by a grant.
    [[nodiscard]] const SpotShadowView& SlotRecord(std::uint32_t slot) const
    {
        return SpotRendered[slot];
    }
    [[nodiscard]] const PointShadowView& PointSlotRecord(std::uint32_t slot) const
    {
        return PointRendered[slot];
    }
    [[nodiscard]] SpotShadowSlotInfo SlotInfo(std::uint32_t slot) const;
    [[nodiscard]] PointShadowSlotInfo PointSlotInfo(std::uint32_t slot) const;
    [[nodiscard]] const ShadowFrameStats& FrameStats() const { return Stats; }
    // Upper bounds for the frame's uniform slot arrays (highest live slot + 1).
    [[nodiscard]] std::uint32_t SlotHighWater() const;
    [[nodiscard]] std::uint32_t PointSlotHighWater() const;
    [[nodiscard]] bool HasOnChangeSlots() const;
    [[nodiscard]] std::uint32_t LiveSlotCount() const;
    [[nodiscard]] std::uint32_t LivePointSlotCount() const;

    // Marks every rendered slot for re-render (render.shadow.invalidate and
    // editor edits that bypass extracted state).
    void InvalidateAll();
    // A scheduled view whose recording failed (frame scratch exhausted)
    // re-queues instead of being treated as rendered.
    void MarkViewFailed(std::uint32_t slot);
    // Only the failed face re-queues; the slot's other faces already match
    // its rendered record. The grant is withheld for the failure frame and
    // returns from cached faces while the face waits to re-render.
    void MarkPointFaceFailed(std::uint32_t slot, std::uint32_t face);
    void Reset();

private:
    void IntakeEvents(std::span<const ShadowCasterEvent> events);
    void MatchRequests(std::span<const SpotShadowRequest> requests);
    void MatchPointRequests(std::span<const PointShadowRequest> requests);
    void EnforceSlotBudget(std::uint32_t maxSlots);
    void EnforcePointSlotBudget(std::uint32_t maxSlots);
    void GrantFreeSlots(std::span<const SpotShadowRequest> requests,
                        std::uint32_t maxSlots);
    void GrantFreePointSlots(std::span<const PointShadowRequest> requests,
                             std::uint32_t maxSlots);
    void ApplyHysteresisAndSteals(std::span<const SpotShadowRequest> requests);
    void ApplyPointHysteresisAndSteals(std::span<const PointShadowRequest> requests);
    void ScheduleViews(std::span<const SpotShadowRequest> requests,
                       std::span<const PointShadowRequest> pointRequests,
                       const ShadowResidencyBudgets& budgets);
    void BuildGrants(std::span<const SpotShadowRequest> requests,
                     std::span<const PointShadowRequest> pointRequests);

    void AcquireSlot(ShadowSlotState& slot, const SpotShadowRequest& request,
                     std::uint32_t requestIndex,
                     const ShadowAtlasAllocation& allocation);
    void AcquirePointSlot(ShadowSlotState& slot, const PointShadowRequest& request,
                          std::uint32_t requestIndex);
    void ReleaseSlot(ShadowSlotState& slot);
    void MarkInvalid(ShadowSlotState& slot);
    void MarkPointInvalid(ShadowSlotState& slot);
    [[nodiscard]] ShadowAtlasAllocation AllocateWithDowngrade(std::uint32_t tileSize);
    [[nodiscard]] bool IsRequestGranted(std::uint32_t requestIndex) const;
    [[nodiscard]] bool IsPointRequestGranted(std::uint32_t requestIndex) const;

    // The pool-neutral slot state (ShadowSlotPool.h) plus the typed extras
    // beside it, indexed by slot: what makes a spot slot a spot slot lives
    // here, not in a second state struct.
    ShadowSlotState Slots[kMaxSpotShadows];
    ShadowSlotState PointSlots[kMaxPointShadows];
    ShadowAtlasAllocation SpotAllocations[kMaxSpotShadows];
    SpotShadowView SpotRendered[kMaxSpotShadows];
    PointShadowView PointRendered[kMaxPointShadows];
    ShadowAtlasAllocator Atlas;
    std::vector<SpotShadowGrant> FrameGrants;
    std::vector<PointShadowGrant> FramePointGrants;
    std::vector<SpotShadowViewJob> FrameViews;
    std::vector<PointShadowFaceJob> FramePointFaces;
    ShadowFrameStats Stats;
    std::uint32_t FrameNumber = 0;
};
