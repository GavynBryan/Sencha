from pathlib import Path

cpp = Path("engine/src/world/RuntimeWorld.cpp")
text = cpp.read_text()
marker = '''std::size_t RuntimeWorld::ZoneCount() const
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

'''
addition = marker + '''bool RuntimeWorld::MoveEntityToZone(EntityId entity, ZoneId destination)
{
    const RuntimeZoneRecord* record = FindZone(destination);
    if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
        return false;
    return Entities_.MoveEntityToPartition(entity, record->Partition);
}

bool RuntimeWorld::MoveEntityToPersistent(EntityId entity)
{
    return Entities_.MoveEntityToPartition(
        entity,
        PersistentStoragePartition);
}

'''
if text.count(marker) != 1:
    raise RuntimeError("RuntimeWorld::ZoneCount changed unexpectedly")
cpp.write_text(text.replace(marker, addition, 1))

for path in (
    Path("tools/temporary/add_runtime_world_migration_api.py"),
    Path(".github/workflows/add-runtime-world-migration-api.yml"),
):
    path.unlink()
