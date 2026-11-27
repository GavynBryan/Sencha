#pragma once

#include <ecs/EntityId.h>

#include <cstdint>
#include <optional>

class HeldRecordIndex;
class World;

// Idempotent. Records follow their holder's storage partition from then on.
HeldRecordIndex& InstallHeldRecords(World& world);

// Authority only. Lists `record` under `holder` (invalid: the world) at
// `position`, or after the holder's last record, and settles it at once.
void HoldRecord(World& world, EntityId record, EntityId holder, std::optional<std::uint16_t> position = {});

// Owner thread, outside any query: docs/gameplay/held-records.md, "Settling".
void SettleHeldRecords(World& world);

// Authority only: destroys every listed record, at once.
void DestroyHeldRecords(World& world);
