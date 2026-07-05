#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneDemand.h>
#include <zone/ZoneRuntime.h>

// How one zone's cooked refs become a registry. The game owns this knowledge
// (component registration, scene deserialization, collision restore); the
// policy layer only decides WHEN a zone loads.
struct ZoneLoadRecipe
{
    AsyncZoneLoader::BuildFn      Build;
    AsyncZoneLoader::FinalizeFn   Finalize;
    std::shared_ptr<AssetPreload> Preload;   // null when the zone preloads nothing
};

using ZoneLoadRecipeFn = std::function<ZoneLoadRecipe(const ZoneHeader&)>;

// The metadata and policy layer over ZoneRuntime: owns the cooked manifest and
// decides desired residency; never deserializes scenes, never owns registries.
// Single-threaded by contract: every method runs on the owner (main) thread.
class WorldPartitionRuntime
{
public:
    WorldPartitionRuntime(ZoneLoadRecipeFn recipe, WorldPartitionStreamingConfig config);

    // Cooked manifest in; adjacency index built here. Refuses (false, message
    // in *error) when any zone lacks a CookedSceneRef or when validation
    // reports any Error-severity record: a broken manifest fails at load time,
    // not mid-traversal.
    bool LoadManifest(WorldPartitionManifest manifest, std::string* error);
    [[nodiscard]] bool HasManifest() const { return HasManifest_; }
    [[nodiscard]] const WorldPartitionManifest& Manifest() const;   // asserts HasManifest

    // The one policy input. Position resolution: candidate zones are those
    // whose Bounds contain the position; the current focus wins if it is a
    // candidate (hysteresis at doorway thresholds); otherwise the
    // smallest-volume candidate, ties broken by ascending zone id; no candidate
    // keeps the previous focus (sticky: bounds gaps and overhangs are normal
    // geometry, not focus changes).
    void SetFocus(Vec3d position);
    // For when position is not meaningful (menus, scripted warps). Asserts the
    // zone exists in the manifest.
    void SetFocus(ZoneId zone);
    [[nodiscard]] ZoneId FocusZone() const { return Focus_; }

    void PinZone(ZoneId zone, ZoneParticipation minimum);
    void UnpinZone(ZoneId zone);

    // The active world-state gameplay tags (dotted names) gating RequiredTags
    // edges. The game pushes the full set whenever its world state changes
    // (quest flags, unlocks); demand reflows on the next Update.
    void SetWorldTags(std::vector<std::string> tags);

    // Once per frame from the owning game system. Computes demand, layers
    // linger state, and diffs desired against resident plus in-flight: issues
    // dormant BeginLoad through the recipe, SetParticipation changes,
    // CancelLoad for undemanded in-flight loads, and RequestDestroy for zones
    // whose linger expired (destruction lands at the next commit drain, never
    // mid-frame: the in-flight frame view stays untouched). Never touches the
    // focus zone's residency.
    void Update(double deltaSeconds, AsyncZoneLoader& loader, ZoneRuntime& zones);

    // Why is this zone resident: rebuilt every Update, includes Lingering
    // entries, ascending zone id. The surface the streaming telemetry writer
    // serializes; coordinate there rather than duplicating a record type.
    [[nodiscard]] std::span<const ZoneDemandRecord> DemandRecords() const { return Records_; }

private:
    struct LingerState
    {
        ZoneId Zone;
        double Seconds = 0.0;
    };

    [[nodiscard]] const ZoneHeader* FindHeader(ZoneId zone) const;

    ZoneLoadRecipeFn Recipe_;
    WorldPartitionStreamingConfig Config_;
    WorldPartitionManifest Manifest_;
    WorldPartitionIndex Index_;
    bool HasManifest_ = false;
    ZoneId Focus_;
    // Last known focus position for proximity demand: the position handed to
    // SetFocus(Vec3d), or the focus zone's bounds center after SetFocus(ZoneId).
    Vec3d FocusPosition_{};
    bool HasFocusPosition_ = false;
    std::vector<ZonePin> Pins_;
    std::vector<std::string> WorldTags_;
    // Zones whose destruction is queued for the next drain; still resident
    // until it runs, so Update must not re-request them.
    std::vector<ZoneId> PendingDestroys_;
    // Zones this runtime has issued BeginLoad for and not yet seen attach or
    // cancel; the enumerable half of AsyncZoneLoader::IsLoading.
    std::vector<ZoneId> Issued_;
    std::vector<LingerState> Lingering_;
    std::vector<ZoneDemandRecord> Records_;
};
