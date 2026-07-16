#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Sphere.h>
#include <render/LightComponentTypes.h>
#include <render/RenderEntityKey.h>
#include <render/RenderLight.h>
#include <render/ShadowAtlasAllocator.h>
#include <render/ShadowCasterSet.h>

#include <cstdint>
#include <span>
#include <vector>

// One packed spot light asking for a shadow this frame. Requests arrive in
// the extraction's deterministic order (score descending, stable key ties).
struct SpotShadowRequest
{
    RenderEntityKey Key;
    std::uint32_t LightIndex = UINT32_MAX;
    float Score = 0.0f;
    std::uint32_t TileSize = kSpotShadowTileExtent;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    // Hash of every extracted value that changes the rendered depth view;
    // a mismatch against the slot's rendered hash re-renders OnChange slots.
    std::uint64_t StateHash = 0;
    Mat4 ViewProjection = Mat4::Identity();
    Vec4 SamplingParams;
    // Cone-bounding volume, intersected against caster-diff event bounds.
    Sphere Bounds;
};

// Hash of every extracted value the rendered depth view depends on, for
// SpotShadowRequest::StateHash. The game extraction and the editor's scene
// gather must produce identical hashes for identical light state, so this is
// the one implementation.
[[nodiscard]] std::uint64_t HashSpotShadowState(const SpotShadowView& view,
                                                std::uint32_t tileSize);

// One depth render scheduled this frame: which slot, where in the atlas, and
// the view-projection to render with.
struct SpotShadowViewJob
{
    std::uint32_t SlotIndex = UINT32_MAX;
    ShadowAtlasAllocation Allocation;
    Mat4 ViewProjection = Mat4::Identity();
};

// Light-to-slot wiring for the frame's packed light list.
struct SpotShadowGrant
{
    std::uint32_t LightIndex = UINT32_MAX;
    std::uint32_t SlotIndex = UINT32_MAX;
};

struct ShadowResidencyBudgets
{
    std::uint32_t MaxSlots = kMaxSpotShadows;
    // Per-frame depth-render clamp; zero means unlimited.
    std::uint32_t MaxViewsPerFrame = 12;
    // Views reserved each frame for the oldest invalidated cached slots, so
    // saturated EveryFrame demand cannot starve them.
    std::uint32_t MinInvalidatedViewsPerFrame = 1;
};

// Read-only view of one arbiter slot, for budget readouts and atlas debug
// displays. Ages are in Update calls (frames); FramesSinceRendered is
// meaningful only while EverRendered.
struct SpotShadowSlotInfo
{
    bool Live = false;
    RenderEntityKey Owner;
    ShadowAtlasAllocation Allocation;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    bool EverRendered = false;
    bool Invalid = false;
    std::uint32_t FramesSinceAcquired = 0;
    std::uint32_t FramesSinceRendered = 0;
};

// Counters for the last Update: how demand met the budgets and how well the
// cache held (a cached slot is a held request whose tile needed no re-render).
struct SpotShadowFrameStats
{
    std::uint32_t RequestCount = 0;
    std::uint32_t HeldRequests = 0;
    std::uint32_t DeniedRequests = 0;
    std::uint32_t ViewsScheduled = 0;
    std::uint32_t CachedSlots = 0;
};

//=============================================================================
// ShadowResidency
//
// The spot shadow slot arbiter: owns which light holds which atlas tile,
// when a cached tile re-renders, and which depth views run this frame.
// Deterministic and CPU-only: identical request/event sequences produce
// identical slot assignment, atlas placement, and view schedules.
//
// Scoring and hysteresis: a slot holder's score gets a fixed multiplier, and
// a contender must outscore a holder for a fixed run of consecutive frames
// before the slot is stolen, so equal-importance lights cannot flicker
// ownership. Tier requests are served through downgrade before denial.
//
// Update policies: EveryFrame slots re-render whenever scheduled, OnChange
// slots re-render on state-hash changes or intersecting caster events, and
// Static slots re-render only through InvalidateAll (the explicit console
// path). Scheduling follows the bounded-fair order: never-rendered slots
// first, a reserved allotment for the oldest invalidated cached slots, then
// EveryFrame work, then remaining invalidated work.
//
// Uniform-facing slot records hold what was last rendered, not what was last
// requested, so a cached tile is always sampled with the matrix it was drawn
// with.
//=============================================================================
class ShadowResidency
{
public:
    static constexpr float kHolderScoreMultiplier = 1.25f;
    static constexpr std::uint32_t kStealOutscoredFrames = 30;

    void Update(std::span<const SpotShadowRequest> requests,
                std::span<const ShadowCasterEvent> events,
                const ShadowResidencyBudgets& budgets);

    [[nodiscard]] std::span<const SpotShadowGrant> Grants() const { return FrameGrants; }
    [[nodiscard]] std::span<const SpotShadowViewJob> ScheduledViews() const { return FrameViews; }

    // Writes the frame's grants onto the packed light set: shadow indices for
    // granted lights, the slot high water, and the last-rendered slot records.
    void ApplyGrants(RenderLightSet& lights) const;

    // The GPU record for one slot as last rendered; meaningful only for
    // slots referenced by a grant.
    [[nodiscard]] const SpotShadowView& SlotRecord(std::uint32_t slot) const
    {
        return Slots[slot].Rendered;
    }
    [[nodiscard]] SpotShadowSlotInfo SlotInfo(std::uint32_t slot) const;
    [[nodiscard]] const SpotShadowFrameStats& FrameStats() const { return Stats; }
    // Upper bound for the frame's uniform slot array (highest live slot + 1).
    [[nodiscard]] std::uint32_t SlotHighWater() const;
    [[nodiscard]] bool HasOnChangeSlots() const;
    [[nodiscard]] std::uint32_t LiveSlotCount() const;

    // Marks every rendered slot for re-render (render.shadow.invalidate and
    // editor edits that bypass extracted state).
    void InvalidateAll();
    // A scheduled view whose recording failed (frame scratch exhausted)
    // re-queues instead of being treated as rendered.
    void MarkViewFailed(std::uint32_t slot);
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

    void IntakeEvents(std::span<const ShadowCasterEvent> events);
    void MatchRequests(std::span<const SpotShadowRequest> requests);
    void EnforceSlotBudget(std::uint32_t maxSlots);
    void GrantFreeSlots(std::span<const SpotShadowRequest> requests,
                        std::uint32_t maxSlots);
    void ApplyHysteresisAndSteals(std::span<const SpotShadowRequest> requests);
    void ScheduleViews(std::span<const SpotShadowRequest> requests,
                       const ShadowResidencyBudgets& budgets);
    void BuildGrants(std::span<const SpotShadowRequest> requests);

    void AcquireSlot(Slot& slot, const SpotShadowRequest& request,
                     std::uint32_t requestIndex,
                     const ShadowAtlasAllocation& allocation);
    void ReleaseSlot(Slot& slot);
    void MarkInvalid(Slot& slot);
    [[nodiscard]] ShadowAtlasAllocation AllocateWithDowngrade(std::uint32_t tileSize);
    [[nodiscard]] bool IsRequestGranted(std::uint32_t requestIndex) const;

    Slot Slots[kMaxSpotShadows];
    ShadowAtlasAllocator Atlas;
    std::vector<SpotShadowGrant> FrameGrants;
    std::vector<SpotShadowViewJob> FrameViews;
    SpotShadowFrameStats Stats;
    std::uint32_t FrameNumber = 0;
};
