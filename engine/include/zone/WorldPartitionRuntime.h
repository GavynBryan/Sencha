#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <zone/AsyncZoneLoader.h>
#include <zone/DockCrossing.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneDemand.h>

class RuntimeWorld;

struct ZoneLoadRecipe
{
    AsyncZoneLoader::BuildFn      Build;
    AsyncZoneLoader::FinalizeFn   Finalize;
    std::shared_ptr<AssetPreload> Preload;
};

using ZoneLoadRecipeFn = std::function<ZoneLoadRecipe(const ZoneHeader&)>;

// Generational token for one runtime participation floor. Several independent
// holders may lease the same zone; their minimum flags compose by union, and
// release order does not matter. A stale token never releases a reused slot.
struct ParticipationLeaseId
{
    static constexpr uint32_t kInvalid = 0xffffffffu;

    uint32_t Index = kInvalid;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != kInvalid; }
    friend bool operator==(ParticipationLeaseId, ParticipationLeaseId) = default;
};

// Metadata and policy layer over RuntimeWorld zone partitions. It owns cooked
// streaming demand, authored pins, and runtime participation leases; it never
// owns entity storage or backend state. Single-threaded by contract.
class WorldPartitionRuntime
{
public:
    WorldPartitionRuntime(ZoneLoadRecipeFn recipe, WorldPartitionStreamingConfig config);

    bool LoadManifest(WorldPartitionManifest manifest, std::string* error);
    [[nodiscard]] bool HasManifest() const { return HasManifest_; }
    [[nodiscard]] const WorldPartitionManifest& Manifest() const;

    // The one policy input. The first position resolves against coarse Zone
    // AABBs. Later movement is swept through authored dock planes during
    // Update, where destination physics residency can be verified.
    void SetFocus(Vec3d position);
    // Optional runtime-only focus shape for conservative late-residency
    // clamping. Height is the total capsule height; nothing is authored or
    // serialized on a Dock.
    void SetFocusCapsule(float radius, float height);
    // Explicit placement/recovery path for teleports, save restore, and
    // out-of-world fallback. Resolves coarse AABBs instead of sweeping docks.
    void RelocateFocus(Vec3d position);
    // For when position is not meaningful (menus, scripted warps). Asserts the
    // zone exists in the manifest.
    void SetFocus(ZoneId zone);
    [[nodiscard]] ZoneId FocusZone() const { return Focus_; }
    [[nodiscard]] std::span<const DockEndpoint> DocksFrom(ZoneId zone) const;
    [[nodiscard]] std::span<const LinkEndpoint> LinksFrom(ZoneId zone) const;
    [[nodiscard]] const GraphRecord* FindGraph(GraphId graph) const;
    [[nodiscard]] const ZoneHeader* FindZone(ZoneId zone) const;
    [[nodiscard]] std::optional<ZoneId> ZoneAt(Vec3d position) const;
    [[nodiscard]] ZoneContainmentResult ResolveZoneAt(
        Vec3d position, ZoneId preferred = {}) const;
    [[nodiscard]] const std::optional<DockTraversalResult>& LastCrossing() const
    {
        return LastCrossing_;
    }
    [[nodiscard]] const DockTraversalResult& LastTraversal() const
    {
        return LastTraversal_;
    }
    [[nodiscard]] uint64_t LateTraversalCount() const { return LateTraversalCount_; }

    // Authored/scripted long-lived floor. Existing semantics remain last-writer-
    // wins because this API names one pin per zone.
    void PinZone(ZoneId zone, ZoneParticipation minimum);
    void UnpinZone(ZoneId zone);

    // Runtime composable floor for relationships and transient work. Leases are
    // game-held: a physics component does not silently retain zones. Unknown
    // zones are rejected; stale release is a safe false result.
    [[nodiscard]] ParticipationLeaseId AcquireParticipationLease(
        ZoneId zone,
        ZoneParticipation minimum);
    bool ReleaseParticipationLease(ParticipationLeaseId lease);
    [[nodiscard]] bool IsParticipationLeaseValid(ParticipationLeaseId lease) const;

    // Forced teardown overrides leases. The owner initiating that teardown must
    // invalidate the affected zone's tokens before destroying it, so holders can
    // observe the loss and stale tokens can never release a reused slot.
    std::size_t InvalidateParticipationLeases(ZoneId zone);

    [[nodiscard]] std::size_t ParticipationLeaseCount() const { return ActiveLeaseCount_; }

    // Once per frame from the owning game system. Computes demand, layers
    // linger state, and diffs desired against resident plus in-flight: issues
    // dormant BeginLoad through the recipe, SetParticipation changes,
    // CancelLoad for undemanded in-flight loads, and RequestDestroy for zones
    // whose linger expired (destruction lands at the next commit drain, never
    // mid-frame: the in-flight frame view stays untouched). Never touches the
    // focus zone's residency.
    void Update(double deltaSeconds, AsyncZoneLoader& loader, RuntimeWorld& world);

    [[nodiscard]] std::span<const ZoneDemandRecord> DemandRecords() const { return Records_; }

    // True while a refused load is being withheld from reissue. Demand cannot
    // express this on its own: a demanded, non-resident zone is indistinguishable
    // from one that will never load, so reissuing from demand alone rebuilds a
    // broken zone every frame. Suppression lifts when the zone's cooked content
    // hash changes, so a recook recovers without restarting.
    [[nodiscard]] bool IsZoneLoadSuppressed(ZoneId zone) const;
    [[nodiscard]] std::size_t SuppressedLoadCount() const
    {
        return FailedLoads_.size();
    }

private:
    struct LingerState
    {
        ZoneId Zone;
        double Seconds = 0.0;
    };

    // A refusal plus the content identity it applies to.
    struct FailedLoad
    {
        ZoneId   Zone;
        uint64_t ContentHash = 0;
    };

    struct ParticipationLeaseSlot
    {
        uint32_t Generation = 1;
        bool Alive = false;
        ZoneId Zone;
        ZoneParticipation Minimum;
    };

    [[nodiscard]] const ZoneHeader* FindHeader(ZoneId zone) const;
    // Adopts refusals the loader recorded, and lifts suppression for zones whose
    // content changed or which left the manifest.
    void ReconcileFailedLoads(AsyncZoneLoader& loader);

    ZoneLoadRecipeFn Recipe_;
    WorldPartitionStreamingConfig Config_;
    WorldPartitionManifest Manifest_;
    WorldPartitionIndex Index_;
    bool HasManifest_ = false;
    ZoneId Focus_;
    Vec3d FocusPosition_{};
    bool HasFocusPosition_ = false;
    Vec3d DockSweepPosition_{};
    Vec3d PendingFocusPosition_{};
    bool HasPendingFocusPosition_ = false;
    float FocusCapsuleRadius_ = 0.0f;
    float FocusCapsuleCylinderHalfHeight_ = 0.0f;
    DockId SuppressedDock_;
    DockTraversalResult LastTraversal_;
    std::optional<DockTraversalResult> LastCrossing_;
    uint64_t LateTraversalCount_ = 0;
    LingerState TraversalGrace_;
    std::vector<ZonePin> Pins_;
    std::vector<ParticipationLeaseSlot> LeaseSlots_;
    std::vector<uint32_t> FreeLeaseSlots_;
    std::size_t ActiveLeaseCount_ = 0;
    // Zones whose destruction is queued for the next drain; still resident
    // until it runs, so Update must not re-request them.
    std::vector<ZoneId> PendingDestroys_;
    std::vector<ZoneId> Issued_;
    std::vector<LingerState> Lingering_;
    std::vector<ZoneDemandRecord> Records_;
    std::vector<FailedLoad> FailedLoads_;
};
