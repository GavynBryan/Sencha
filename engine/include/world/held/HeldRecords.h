#pragma once

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <zone/ZoneId.h>

#include <cstdint>
#include <optional>

class HeldRecordIndex;
class World;

// Idempotent. Records follow their holder's storage partition from then on.
HeldRecordIndex& InstallHeldRecords(World& world);

// Authority only. Lists `record` under `holder` (invalid: the world) at
// `position`, or after the holder's last record, and settles it at once.
void HoldRecord(World& world, EntityId record, EntityId holder, std::optional<std::uint16_t> position = {});

// docs/gameplay/held-records.md, "Authored records".
void NoteHeldRecordChanged(World& world, EntityId record);

// Owner thread, outside any query: docs/gameplay/held-records.md, "Settling".
void SettleHeldRecords(World& world);

// Before the partition goes: docs/gameplay/held-records.md, "Records of authored holders".
void ParkHeldRecords(World& world, ZoneId zone, StoragePartitionId partition);

void DestroyAwaitingHeldRecords(World& world);

// Leaves records with authored ids.
void DestroyHeldRecords(World& world);
