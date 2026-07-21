#include <world/RuntimeWorld.h>

#include <components/ActiveCameraService.h>
#include <ecs/WorldComponentSchema.h>

#include <algorithm>
#include <cassert>
#include <limits>

RuntimeWorld::RuntimeWorld(const WorldComponentSchema& schema)
{
    schema.Apply(Entities_);

    // Active-camera selection is simulation-scoped under one entity universe,
    // and component hooks must be able to reach World resources during teardown.
    Entities_.AddResource<ActiveCameraService>();

    // Partition zero is persistent and never receives a RuntimeZoneRecord.
    ZonesByPartition_.resize(1);
}

RuntimeWorld::~RuntimeWorld() = default;

RuntimeZoneRecord& RuntimeWorld::AttachZone(
    ZoneId zone,
    ZoneParticipation participation)
{
    assert(!FrameViewLive_
           && "AttachZone with a live FrameZoneView: attach at the lifecycle drain");
    assert(!ResidencyProcessing_
           && "AttachZone during ZoneResidency processing");
    assert(zone.IsValid() && "AttachZone requires a valid ZoneId");
    assert(FindZone(zone) == nullptr && "AttachZone received a duplicate ZoneId");

    const StoragePartitionId partition = AllocatePartition();
    auto record = std::make_unique<RuntimeZoneRecord>();
    record->Id = zone;
    record->Partition = partition;
    record->State = RuntimeZoneLoadState::Resident;
    record->Participation = participation;

    RuntimeZoneRecord* result = record.get();
    ZonesByPartition_[partition.Value] = std::move(record);
    RecordAttached(*result);
    return *result;
}

RuntimeZoneRecord* RuntimeWorld::FindZone(ZoneId zone)
{
    if (!zone.IsValid())
        return nullptr;

    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        RuntimeZoneRecord* record = ZonesByPartition_[index].get();
        if (record != nullptr && record->Id == zone)
            return record;
    }
    return nullptr;
}

const RuntimeZoneRecord* RuntimeWorld::FindZone(ZoneId zone) const
{
    if (!zone.IsValid())
        return nullptr;

    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        const RuntimeZoneRecord* record = ZonesByPartition_[index].get();
        if (record != nullptr && record->Id == zone)
            return record;
    }
    return nullptr;
}

RuntimeZoneRecord* RuntimeWorld::FindPartition(StoragePartitionId partition)
{
    if (partition == PersistentStoragePartition
        || partition.Value >= ZonesByPartition_.size())
    {
        return nullptr;
    }
    return ZonesByPartition_[partition.Value].get();
}

const RuntimeZoneRecord* RuntimeWorld::FindPartition(
    StoragePartitionId partition) const
{
    if (partition == PersistentStoragePartition
        || partition.Value >= ZonesByPartition_.size())
    {
        return nullptr;
    }
    return ZonesByPartition_[partition.Value].get();
}

bool RuntimeWorld::IsZoneResident(ZoneId zone) const
{
    const RuntimeZoneRecord* record = FindZone(zone);
    return record != nullptr && record->State == RuntimeZoneLoadState::Resident;
}

std::size_t RuntimeWorld::ZoneCount() const
{
    std::size_t count = 0;
    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        const RuntimeZoneRecord* record = ZonesByPartition_[index].get();
        if (record != nullptr && record->State == RuntimeZoneLoadState::Resident)
            ++count;
    }
    return count;
}

bool RuntimeWorld::RequestParticipation(
    ZoneId zone,
    ZoneParticipation participation)
{
    const RuntimeZoneRecord* record = FindZone(zone);
    if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
        return false;

    for (ParticipationRequest& request : ParticipationRequests_)
    {
        if (request.Zone == zone)
        {
            request.Participation = participation;
            return true;
        }
    }

    ParticipationRequests_.push_back(ParticipationRequest{ zone, participation });
    return true;
}

bool RuntimeWorld::RequestDetach(ZoneId zone)
{
    const RuntimeZoneRecord* record = FindZone(zone);
    if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
        return false;

    if (std::find(DetachRequests_.begin(), DetachRequests_.end(), zone)
        == DetachRequests_.end())
    {
        DetachRequests_.push_back(zone);
    }
    return true;
}

void RuntimeWorld::FlushLifecycleRequests()
{
    assert(!FrameViewLive_
           && "FlushLifecycleRequests with a live FrameZoneView");
    assert(!ResidencyProcessing_
           && "FlushLifecycleRequests during ZoneResidency processing");

    for (const ParticipationRequest& request : ParticipationRequests_)
    {
        RuntimeZoneRecord* record = FindZone(request.Zone);
        if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
            continue;

        const ZoneParticipation previous = record->Participation;
        if (previous == request.Participation)
            continue;

        record->Participation = request.Participation;
        RecordParticipationChange(
            *record,
            previous,
            request.Participation);
    }
    ParticipationRequests_.clear();

    // Detach wins over any participation request in the same drain window. The
    // pending transition is coalesced to one final Detaching visit.
    for (ZoneId zone : DetachRequests_)
    {
        RuntimeZoneRecord* record = FindZone(zone);
        if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
            continue;

        const ZoneParticipation previous = record->Participation;
        record->State = RuntimeZoneLoadState::Detaching;
        record->Participation = ZoneParticipation{};
        RecordDetaching(*record, previous);
    }
    DetachRequests_.clear();
}

std::span<const ZoneResidencyChange>
RuntimeWorld::PendingResidencyChanges() const
{
    return { PendingChanges_.data(), PendingChanges_.size() };
}

std::span<const ZoneResidencyChange>
RuntimeWorld::BeginResidencyProcessing()
{
    assert(!FrameViewLive_
           && "BeginResidencyProcessing with a live FrameZoneView");
    assert(!ResidencyProcessing_
           && "BeginResidencyProcessing called twice without finalization");

    ProcessingChanges_.clear();
    ProcessingChanges_.swap(PendingChanges_);
    ResidencyProcessing_ = true;
    return { ProcessingChanges_.data(), ProcessingChanges_.size() };
}

void RuntimeWorld::FinalizeResidencyProcessing()
{
    assert(ResidencyProcessing_
           && "FinalizeResidencyProcessing without a processing batch");
    assert(!FrameViewLive_
           && "FinalizeResidencyProcessing with a live FrameZoneView");

    for (const ZoneResidencyChange& change : ProcessingChanges_)
    {
        if (change.Kind != ZoneResidencyChangeKind::Detaching)
            continue;

        RuntimeZoneRecord* record = FindPartition(change.Partition);
        if (record == nullptr)
            continue;

        assert(record->Id == change.Zone);
        assert(record->State == RuntimeZoneLoadState::Detaching);

        // Backend scenes receive the residency batch before this point. Entity
        // hooks then run through the ordinary World destruction path while
        // simulation-scoped World resources and zone resources are still alive.
        (void)Entities_.DestroyPartition(change.Partition);
        ZonesByPartition_[change.Partition.Value].reset();
        ReleasePartition(change.Partition);
    }

    ProcessingChanges_.clear();
    ResidencyProcessing_ = false;
}

FrameZoneView RuntimeWorld::BuildFrameView()
{
    assert(!ResidencyProcessing_
           && "BuildFrameView before residency finalization");
    assert(!FrameViewLive_
           && "BuildFrameView called twice without EndFrameView");

    FrameZoneView view;
    view.Entities = &Entities_;

    view.Resident.Add(PersistentStoragePartition);
    view.Visible.Add(PersistentStoragePartition);
    view.Physics.Add(PersistentStoragePartition);
    view.Logic.Add(PersistentStoragePartition);
    view.Audio.Add(PersistentStoragePartition);

    // Slot order is stable and dense, giving every domain deterministic
    // partition iteration without sorting or allocation in query execution.
    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        const RuntimeZoneRecord* record = ZonesByPartition_[index].get();
        if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
            continue;

        view.Resident.Add(record->Partition);
        if (record->Participation.Visible)
            view.Visible.Add(record->Partition);
        if (record->Participation.Physics)
            view.Physics.Add(record->Partition);
        if (record->Participation.Logic)
            view.Logic.Add(record->Partition);
        if (record->Participation.Audio)
            view.Audio.Add(record->Partition);
    }

    FrameViewLive_ = true;
    return view;
}

void RuntimeWorld::EndFrameView()
{
    assert(FrameViewLive_ && "EndFrameView called without a live FrameZoneView");
    FrameViewLive_ = false;
}

StoragePartitionId RuntimeWorld::AllocatePartition()
{
    if (!FreePartitions_.empty())
    {
        const std::uint16_t value = FreePartitions_.back();
        FreePartitions_.pop_back();
        assert(value != 0);
        assert(value < ZonesByPartition_.size());
        assert(ZonesByPartition_[value] == nullptr);
        return StoragePartitionId{ value };
    }

    assert(ZonesByPartition_.size()
               <= static_cast<std::size_t>(
                   std::numeric_limits<std::uint16_t>::max())
           && "RuntimeWorld exhausted StoragePartitionId values");

    const auto value = static_cast<std::uint16_t>(ZonesByPartition_.size());
    ZonesByPartition_.push_back(nullptr);
    return StoragePartitionId{ value };
}

void RuntimeWorld::ReleasePartition(StoragePartitionId partition)
{
    assert(partition != PersistentStoragePartition);
    assert(partition.Value < ZonesByPartition_.size());
    assert(ZonesByPartition_[partition.Value] == nullptr);
    FreePartitions_.push_back(partition.Value);
}

ZoneResidencyChange* RuntimeWorld::FindPendingChange(
    StoragePartitionId partition)
{
    for (ZoneResidencyChange& change : PendingChanges_)
        if (change.Partition == partition)
            return &change;
    return nullptr;
}

void RuntimeWorld::RecordAttached(const RuntimeZoneRecord& record)
{
    assert(FindPendingChange(record.Partition) == nullptr);
    PendingChanges_.push_back(ZoneResidencyChange{
        ZoneResidencyChangeKind::Attached,
        record.Id,
        record.Partition,
        ZoneParticipation{},
        record.Participation,
    });
}

void RuntimeWorld::RecordParticipationChange(
    const RuntimeZoneRecord& record,
    ZoneParticipation previous,
    ZoneParticipation current)
{
    ZoneResidencyChange* existing = FindPendingChange(record.Partition);
    if (existing == nullptr)
    {
        PendingChanges_.push_back(ZoneResidencyChange{
            ZoneResidencyChangeKind::ParticipationChanged,
            record.Id,
            record.Partition,
            previous,
            current,
        });
        return;
    }

    if (existing->Kind == ZoneResidencyChangeKind::Detaching)
        return;

    existing->Current = current;
    if (existing->Kind == ZoneResidencyChangeKind::ParticipationChanged
        && existing->Previous == existing->Current)
    {
        const StoragePartitionId partition = existing->Partition;
        std::erase_if(
            PendingChanges_,
            [partition](const ZoneResidencyChange& change) {
                return change.Partition == partition;
            });
    }
}

void RuntimeWorld::RecordDetaching(
    const RuntimeZoneRecord& record,
    ZoneParticipation previous)
{
    ZoneResidencyChange* existing = FindPendingChange(record.Partition);
    if (existing == nullptr)
    {
        PendingChanges_.push_back(ZoneResidencyChange{
            ZoneResidencyChangeKind::Detaching,
            record.Id,
            record.Partition,
            previous,
            ZoneParticipation{},
        });
        return;
    }

    if (existing->Kind == ZoneResidencyChangeKind::Attached)
        existing->Previous = existing->Current;

    existing->Kind = ZoneResidencyChangeKind::Detaching;
    existing->Current = ZoneParticipation{};
}
