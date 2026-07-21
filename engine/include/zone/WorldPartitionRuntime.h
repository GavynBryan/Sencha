#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <zone/AsyncZoneLoader.h>
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

    void SetFocus(Vec3d position);
    void SetFocus(ZoneId zone);
    [[nodiscard]] ZoneId FocusZone() const { return Focus_; }

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

    void SetWorldTags(std::vector<std::string> tags);

    void Update(double deltaSeconds, AsyncZoneLoader& loader, RuntimeWorld& world);

    [[nodiscard]] std::span<const ZoneDemandRecord> DemandRecords() const { return Records_; }

private:
    struct LingerState
    {
        ZoneId Zone;
        double Seconds = 0.0;
    };

    struct ParticipationLeaseSlot
    {
        uint32_t Generation = 1;
        bool Alive = false;
        ZoneId Zone;
        ZoneParticipation Minimum;
    };

    [[nodiscard]] const ZoneHeader* FindHeader(ZoneId zone) const;

    ZoneLoadRecipeFn Recipe_;
    WorldPartitionStreamingConfig Config_;
    WorldPartitionManifest Manifest_;
    WorldPartitionIndex Index_;
    bool HasManifest_ = false;
    ZoneId Focus_;
    Vec3d FocusPosition_{};
    bool HasFocusPosition_ = false;
    std::vector<ZonePin> Pins_;
    std::vector<ParticipationLeaseSlot> LeaseSlots_;
    std::vector<uint32_t> FreeLeaseSlots_;
    std::size_t ActiveLeaseCount_ = 0;
    std::vector<std::string> WorldTags_;
    std::vector<ZoneId> PendingDestroys_;
    std::vector<ZoneId> Issued_;
    std::vector<LingerState> Lingering_;
    std::vector<ZoneDemandRecord> Records_;
};
