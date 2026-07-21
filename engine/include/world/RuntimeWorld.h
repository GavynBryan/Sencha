#pragma once

#include <ecs/StoragePartitionId.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/ResourceRegistry.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class WorldComponentSchema;

// Runtime partition zero is reserved for entities whose lifetime is the whole
// simulation rather than one streamed zone.
inline constexpr StoragePartitionId PersistentStoragePartition =
    StoragePartitionId::Default();

enum class RuntimeZoneLoadState : std::uint8_t
{
    Loading,
    Importing,
    Resident,
    Detaching,
};

enum class ZoneResidencyChangeKind : std::uint8_t
{
    Attached,
    ParticipationChanged,
    Detaching,
};

struct ZoneResidencyChange
{
    ZoneResidencyChangeKind Kind = ZoneResidencyChangeKind::Attached;
    ZoneId Zone;
    StoragePartitionId Partition = PersistentStoragePartition;
    ZoneParticipation Previous;
    ZoneParticipation Current;
};

// One frame's stable participation view over a single entity World. The
// persistent partition is included in every domain set by construction.
struct FrameZoneView
{
    World* Entities = nullptr;
    StoragePartitionSet Visible;
    StoragePartitionSet Physics;
    StoragePartitionSet Logic;
    StoragePartitionSet Audio;
    StoragePartitionSet Resident;
};

// Runtime metadata and strictly zone-scoped resources for one resident zone.
// Backend handles and entity-indexed state do not belong here.
struct RuntimeZoneRecord
{
    ZoneId Id;
    StoragePartitionId Partition = PersistentStoragePartition;
    RuntimeZoneLoadState State = RuntimeZoneLoadState::Resident;
    ZoneParticipation Participation;
    ResourceRegistry Resources;
};

// One runtime entity universe with streamed zones represented as storage
// partitions. This coexists with the current per-registry ZoneRuntime until the
// migration's scheduling, loading, and backend phases are proven.
class RuntimeWorld
{
public:
    explicit RuntimeWorld(const WorldComponentSchema& schema);
    ~RuntimeWorld();

    RuntimeWorld(const RuntimeWorld&) = delete;
    RuntimeWorld& operator=(const RuntimeWorld&) = delete;
    RuntimeWorld(RuntimeWorld&&) = delete;
    RuntimeWorld& operator=(RuntimeWorld&&) = delete;

    [[nodiscard]] World& Entities() { return Entities_; }
    [[nodiscard]] const World& Entities() const { return Entities_; }

    RuntimeZoneRecord& AttachZone(
        ZoneId zone,
        ZoneParticipation participation = {});

    [[nodiscard]] RuntimeZoneRecord* FindZone(ZoneId zone);
    [[nodiscard]] const RuntimeZoneRecord* FindZone(ZoneId zone) const;
    [[nodiscard]] RuntimeZoneRecord* FindPartition(StoragePartitionId partition);
    [[nodiscard]] const RuntimeZoneRecord* FindPartition(
        StoragePartitionId partition) const;

    [[nodiscard]] bool IsZoneResident(ZoneId zone) const;
    [[nodiscard]] std::size_t ZoneCount() const;

    // Requests are legal while a frame view or residency batch is live. They do
    // not mutate current participation. FlushLifecycleRequests applies the
    // coalesced requests at the next owner-thread lifecycle drain.
    bool RequestParticipation(ZoneId zone, ZoneParticipation participation);
    bool RequestDetach(ZoneId zone);
    void FlushLifecycleRequests();

    [[nodiscard]] std::span<const ZoneResidencyChange> PendingResidencyChanges()
        const;
    [[nodiscard]] std::span<const ZoneResidencyChange>
        BeginResidencyProcessing();
    void FinalizeResidencyProcessing();

    [[nodiscard]] FrameZoneView BuildFrameView();
    void EndFrameView();

private:
    struct ParticipationRequest
    {
        ZoneId Zone;
        ZoneParticipation Participation;
    };

    StoragePartitionId AllocatePartition();
    void ReleasePartition(StoragePartitionId partition);

    ZoneResidencyChange* FindPendingChange(StoragePartitionId partition);
    void RecordAttached(const RuntimeZoneRecord& record);
    void RecordParticipationChange(
        const RuntimeZoneRecord& record,
        ZoneParticipation previous,
        ZoneParticipation current);
    void RecordDetaching(
        const RuntimeZoneRecord& record,
        ZoneParticipation previous);

    World Entities_;

    // Indexed directly by StoragePartitionId. Slot zero is permanently reserved
    // for persistent entities and never contains a RuntimeZoneRecord.
    std::vector<std::unique_ptr<RuntimeZoneRecord>> ZonesByPartition_;
    std::vector<std::uint16_t> FreePartitions_;

    std::vector<ParticipationRequest> ParticipationRequests_;
    std::vector<ZoneId> DetachRequests_;

    std::vector<ZoneResidencyChange> PendingChanges_;
    std::vector<ZoneResidencyChange> ProcessingChanges_;

    bool ResidencyProcessing_ = false;
    bool FrameViewLive_ = false;
};
