from pathlib import Path

cpp = Path("engine/src/world/RuntimeWorld.cpp")
text = cpp.read_text()

old = '''RuntimeZoneRecord& RuntimeWorld::AttachZone(
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
    const auto [zoneIt, inserted] = PartitionByZone_.emplace(zone, partition);
    (void)zoneIt;
    assert(inserted && "AttachZone failed to index a unique ZoneId");
    RecordAttached(*result);
    return *result;
}
'''
new = '''RuntimeZoneRecord& RuntimeWorld::BeginZoneImport(ZoneId zone)
{
    assert(!FrameViewLive_
           && "BeginZoneImport with a live FrameZoneView: import at the lifecycle drain");
    assert(!ResidencyProcessing_
           && "BeginZoneImport during ZoneResidency processing");
    assert(zone.IsValid() && "BeginZoneImport requires a valid ZoneId");
    assert(FindZone(zone) == nullptr
           && "BeginZoneImport received a duplicate ZoneId");

    const StoragePartitionId partition = AllocatePartition();
    auto record = std::make_unique<RuntimeZoneRecord>();
    record->Id = zone;
    record->Partition = partition;
    record->State = RuntimeZoneLoadState::Importing;
    record->Participation = ZoneParticipation{};

    RuntimeZoneRecord* result = record.get();
    ZonesByPartition_[partition.Value] = std::move(record);
    const auto [zoneIt, inserted] = PartitionByZone_.emplace(zone, partition);
    (void)zoneIt;
    assert(inserted && "BeginZoneImport failed to index a unique ZoneId");
    return *result;
}

bool RuntimeWorld::PublishZone(
    ZoneId zone,
    ZoneParticipation participation)
{
    assert(!FrameViewLive_
           && "PublishZone with a live FrameZoneView: publish at the lifecycle drain");
    assert(!ResidencyProcessing_
           && "PublishZone during ZoneResidency processing");

    RuntimeZoneRecord* record = FindZone(zone);
    if (record == nullptr || record->State != RuntimeZoneLoadState::Importing)
        return false;

    record->State = RuntimeZoneLoadState::Resident;
    record->Participation = participation;
    RecordAttached(*record);
    return true;
}

bool RuntimeWorld::CancelZoneImport(ZoneId zone)
{
    assert(!FrameViewLive_
           && "CancelZoneImport with a live FrameZoneView");
    assert(!ResidencyProcessing_
           && "CancelZoneImport during ZoneResidency processing");

    RuntimeZoneRecord* record = FindZone(zone);
    if (record == nullptr || record->State != RuntimeZoneLoadState::Importing)
        return false;

    const StoragePartitionId partition = record->Partition;
    (void)Entities_.DestroyPartition(partition);
    const std::size_t erased = PartitionByZone_.erase(zone);
    assert(erased == 1 && "Cancelled import was missing from ZoneId index");
    ZonesByPartition_[partition.Value].reset();
    ReleasePartition(partition);
    return true;
}

RuntimeZoneRecord& RuntimeWorld::AttachZone(
    ZoneId zone,
    ZoneParticipation participation)
{
    RuntimeZoneRecord& record = BeginZoneImport(zone);
    const bool published = PublishZone(zone, participation);
    assert(published && "AttachZone failed to publish a new import partition");
    return record;
}
'''
if text.count(old) != 1:
    raise RuntimeError("AttachZone implementation changed unexpectedly")
cpp.write_text(text.replace(old, new, 1))

for path in (
    Path("tools/temporary/add_hidden_zone_import.py"),
    Path(".github/workflows/add-hidden-zone-import.yml"),
):
    path.unlink()
