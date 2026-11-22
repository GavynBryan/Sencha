#pragma once

#include <ecs/ArchetypeSignature.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <zone/ZoneStateScope.h>

#include <cstdint>
#include <optional>

class HeldRecordIndex;
class World;

HeldRecordIndex& InstallHeldRecords(World& world);

void HoldRecord(World& world, EntityId record, EntityId holder, std::optional<std::uint16_t> position = {});

void NameHeldHolder(World& world, EntityId record);

// The columns `values` names are left unwritten for World::InitializeComponent.
[[nodiscard]] EntityId PlaceHeldRecord(World& world, EntityId holder, ArchetypeSignature values,
                                       std::optional<std::uint16_t> position = {});

void NoteHeldRecordChanged(World& world, EntityId record);

// Owner thread, outside any query.
void SettleHeldRecords(World& world);

void ParkHeldRecords(World& world, ZoneStateScope scope, StoragePartitionId partition);

// A level's waiting records leave every lookup while another level is loaded.
void SetAsideHeldRecords(World& world, ZoneStateLevelId level);
void ResumeHeldRecords(World& world, ZoneStateLevelId level);
void DestroySetAsideHeldRecords(World& world, ZoneStateLevelId level);

void DestroyHeldRecords(World& world);
