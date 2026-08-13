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

    //-------------------------------------------------------------------------
    // Focus
    //
    // Residency is kept around focus sources, plural. One is the ordinary case
    // -- a player, or an editor preview -- and the unqualified calls below mean
    // that one. An authority keeps one per connected player, and what it holds
    // resident is the union of their neighborhoods: a zone nobody is near can
    // go, a zone somebody is standing in cannot, and a zone one hop from
    // anybody is treated as one hop away.
    //
    // Each source carries its own traversal state, because crossing a doorway
    // is something a player does rather than something the world does. Two
    // players walking through different doors on the same frame is the ordinary
    // case, not a conflict.
    //-------------------------------------------------------------------------

    // The one policy input. The first position resolves against coarse Zone
    // AABBs. Later movement is swept through authored dock planes during
    // Update, where destination physics residency can be verified.
    void SetFocus(Vec3d position) { SetFocus(kPrimaryFocusSource, position); }
    void SetFocus(FocusSourceId source, Vec3d position);
    // Optional runtime-only focus shape for conservative late-residency
    // clamping. Height is the total capsule height; nothing is authored or
    // serialized on a Dock.
    void SetFocusCapsule(float radius, float height)
    {
        SetFocusCapsule(kPrimaryFocusSource, radius, height);
    }
    void SetFocusCapsule(FocusSourceId source, float radius, float height);
    // Explicit placement/recovery path for teleports, save restore, and
    // out-of-world fallback. Resolves coarse AABBs instead of sweeping docks.
    void RelocateFocus(Vec3d position)
    {
        RelocateFocus(kPrimaryFocusSource, position);
    }
    void RelocateFocus(FocusSourceId source, Vec3d position);
    // For when position is not meaningful (menus, scripted warps). Asserts the
    // zone exists in the manifest.
    void SetFocus(ZoneId zone) { SetFocus(kPrimaryFocusSource, zone); }
    void SetFocus(FocusSourceId source, ZoneId zone);

    // Drops a source and everything it was holding open. What it alone demanded
    // enters linger like anything else that stops being demanded, so a player
    // leaving does not tear zones out from under the ones still there.
    // False for a source that was not held.
    bool RemoveFocusSource(FocusSourceId source);

    [[nodiscard]] ZoneId FocusZone() const { return FocusZone(kPrimaryFocusSource); }
    [[nodiscard]] ZoneId FocusZone(FocusSourceId source) const;
    [[nodiscard]] std::size_t FocusSourceCount() const { return Sources_.size(); }
    [[nodiscard]] std::span<const DockEndpoint> DocksFrom(ZoneId zone) const;
    [[nodiscard]] std::span<const LinkEndpoint> LinksFrom(ZoneId zone) const;
    [[nodiscard]] const GraphRecord* FindGraph(GraphId graph) const;
    [[nodiscard]] const ZoneHeader* FindZone(ZoneId zone) const;
    [[nodiscard]] std::optional<ZoneId> ZoneAt(Vec3d position) const;
    [[nodiscard]] ZoneContainmentResult ResolveZoneAt(
        Vec3d position, ZoneId preferred = {}) const;
    // The most recent Update's sweep. Status distinguishes no movement, a
    // completed crossing, and a crossing held back because the destination was
    // not resident; it is reset at the top of every Update.
    [[nodiscard]] const DockTraversalResult& LastTraversal() const
    {
        return LastTraversal(kPrimaryFocusSource);
    }
    // Empty for a source that is not held, which reads the same as a source
    // that did not move.
    [[nodiscard]] const DockTraversalResult& LastTraversal(FocusSourceId source) const;
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

    // Everything about one focus that another focus does not share. All of it
    // was a member of the runtime when there could only be one.
    struct FocusSource
    {
        FocusSourceId Id;
        ZoneId Focus;
        Vec3d Position{};
        bool HasPosition = false;
        // Where the dock sweep last ran from, which is not the same as the
        // focus position: a crossing held back for an unready destination
        // leaves the focus at a safe point while the sweep remembers where it
        // actually got to.
        Vec3d SweepPosition{};
        Vec3d PendingPosition{};
        bool HasPendingPosition = false;
        float CapsuleRadius = 0.0f;
        float CapsuleCylinderHalfHeight = 0.0f;
        DockId SuppressedDock;
        DockTraversalResult LastTraversal;
        // The zone this source just left, held briefly so a doorway crossed at
        // speed does not evict the room behind it.
        LingerState Grace;
    };

    [[nodiscard]] FocusSource& SourceFor(FocusSourceId source);
    [[nodiscard]] const FocusSource* FindSource(FocusSourceId source) const;

    ZoneLoadRecipeFn Recipe_;
    WorldPartitionStreamingConfig Config_;
    WorldPartitionManifest Manifest_;
    WorldPartitionIndex Index_;
    bool HasManifest_ = false;
    // Ordered by source id, so the demand merge and everything derived from it
    // does not depend on the order players happened to arrive in.
    std::vector<FocusSource> Sources_;
    uint64_t LateTraversalCount_ = 0;
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
