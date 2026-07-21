from pathlib import Path

cpp = Path("engine/src/world/RuntimeWorld.cpp")
text = cpp.read_text()

old = '''    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        if (ZonesByPartition_[index] == nullptr)
            continue;

        (void)Entities_.DestroyPartition(
            StoragePartitionId{ static_cast<std::uint16_t>(index) });
        ZonesByPartition_[index].reset();
    }
}
'''
new = '''    for (std::size_t index = 1; index < ZonesByPartition_.size(); ++index)
    {
        if (ZonesByPartition_[index] == nullptr)
            continue;

        (void)Entities_.DestroyPartition(
            StoragePartitionId{ static_cast<std::uint16_t>(index) });
        ZonesByPartition_[index].reset();
    }
    PartitionByZone_.clear();
}
'''
if text.count(old) != 1:
    raise RuntimeError("RuntimeWorld destructor body changed unexpectedly")
text = text.replace(old, new, 1)

old = '''    RuntimeZoneRecord* result = record.get();
    ZonesByPartition_[partition.Value] = std::move(record);
    RecordAttached(*result);
    return *result;
}
'''
new = '''    RuntimeZoneRecord* result = record.get();
    ZonesByPartition_[partition.Value] = std::move(record);
    const auto [zoneIt, inserted] = PartitionByZone_.emplace(zone, partition);
    (void)zoneIt;
    assert(inserted && "AttachZone failed to index a unique ZoneId");
    RecordAttached(*result);
    return *result;
}
'''
if text.count(old) != 1:
    raise RuntimeError("AttachZone publication changed unexpectedly")
text = text.replace(old, new, 1)

old = '''RuntimeZoneRecord* RuntimeWorld::FindZone(ZoneId zone)
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
'''
new = '''RuntimeZoneRecord* RuntimeWorld::FindZone(ZoneId zone)
{
    if (!zone.IsValid())
        return nullptr;

    const auto it = PartitionByZone_.find(zone);
    if (it == PartitionByZone_.end())
        return nullptr;

    RuntimeZoneRecord* record = FindPartition(it->second);
    assert(record != nullptr && record->Id == zone);
    return record;
}

const RuntimeZoneRecord* RuntimeWorld::FindZone(ZoneId zone) const
{
    if (!zone.IsValid())
        return nullptr;

    const auto it = PartitionByZone_.find(zone);
    if (it == PartitionByZone_.end())
        return nullptr;

    const RuntimeZoneRecord* record = FindPartition(it->second);
    assert(record != nullptr && record->Id == zone);
    return record;
}
'''
if text.count(old) != 1:
    raise RuntimeError("FindZone implementations changed unexpectedly")
text = text.replace(old, new, 1)

old = '''        (void)Entities_.DestroyPartition(change.Partition);
        ZonesByPartition_[change.Partition.Value].reset();
        ReleasePartition(change.Partition);
'''
new = '''        (void)Entities_.DestroyPartition(change.Partition);
        const std::size_t erased = PartitionByZone_.erase(change.Zone);
        assert(erased == 1 && "Detaching zone was missing from ZoneId index");
        ZonesByPartition_[change.Partition.Value].reset();
        ReleasePartition(change.Partition);
'''
if text.count(old) != 1:
    raise RuntimeError("FinalizeResidencyProcessing teardown changed unexpectedly")
text = text.replace(old, new, 1)

cpp.write_text(text)

for path in (
    Path("tools/temporary/index_runtime_zones.py"),
    Path(".github/workflows/index-runtime-zones.yml"),
):
    path.unlink()
