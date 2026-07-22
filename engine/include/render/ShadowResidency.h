#pragma once

#include <render/RenderLight.h>
#include <render/ShadowAtlasAllocator.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidencyTypes.h>

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
// rendered at least once.
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
        return Slots[slot].Rendered;
    }
    [[nodiscard]] const PointShadowView& PointSlotRecord(std::uint32_t slot) const
    {
        return PointSlots[slot].Rendered;
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
    // its rendered record. The grant is withheld until the face re-renders.
    void MarkPointFaceFailed(std::uint32_t slot, std::uint32_t face);
    void Reset();

private:
    struct Slot
    {
        bool Live = false;
        RenderEntityKey Owner;
        ShadowAtlasAllocation Allocation;
        ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
        std::uint64_t StateHash = 0;
        Sphere Volume;
        SpotShadowView Rendered;
        bool EverRendered = false;
        bool Invalid = false;
        std::uint32_t AcquiredFrame = 0;
        std::uint32_t LastRenderedFrame = 0;
        std::uint32_t InvalidatedFrame = 0;
        std::uint32_t OutscoredFrames = 0;

        // Per-frame transients, rebuilt by Update.
        std::uint32_t RequestIndex = UINT32_MAX;
        float EffectiveScore = 0.0f;
        bool ScheduledThisFrame = false;
    };

    struct PointSlot
    {
        bool Live = false;
        RenderEntityKey Owner;
        ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
        std::uint64_t StateHash = 0;
        Sphere Volume;
        PointShadowView Rendered;
        std::uint32_t PendingFaces = 0;
        bool EverRendered = false;
        bool Invalid = false;
        std::uint32_t AcquiredFrame = 0;
        std::uint32_t LastRenderedFrame = 0;
        std::uint32_t InvalidatedFrame = 0;
        std::uint32_t OutscoredFrames = 0;

        // Per-frame transients, rebuilt by Update.
        std::uint32_t RequestIndex = UINT32_MAX;
        float EffectiveScore = 0.0f;
        bool ScheduledThisFrame = false;
    };

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

    void AcquireSlot(Slot& slot, const SpotShadowRequest& request,
                     std::uint32_t requestIndex,
                     const ShadowAtlasAllocation& allocation);
    void AcquirePointSlot(PointSlot& slot, const PointShadowRequest& request,
                          std::uint32_t requestIndex);
    void ReleaseSlot(Slot& slot);
    void MarkInvalid(Slot& slot);
    void MarkPointInvalid(PointSlot& slot);
    [[nodiscard]] ShadowAtlasAllocation AllocateWithDowngrade(std::uint32_t tileSize);
    [[nodiscard]] bool IsRequestGranted(std::uint32_t requestIndex) const;
    [[nodiscard]] bool IsPointRequestGranted(std::uint32_t requestIndex) const;

    Slot Slots[kMaxSpotShadows];
    PointSlot PointSlots[kMaxPointShadows];
    ShadowAtlasAllocator Atlas;
    std::vector<SpotShadowGrant> FrameGrants;
    std::vector<PointShadowGrant> FramePointGrants;
    std::vector<SpotShadowViewJob> FrameViews;
    std::vector<PointShadowFaceJob> FramePointFaces;
    ShadowFrameStats Stats;
    std::uint32_t FrameNumber = 0;
};
