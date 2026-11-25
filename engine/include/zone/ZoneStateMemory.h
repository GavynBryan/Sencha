#pragma once

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <zone/ZoneStateScope.h>
#include <zone/ZoneStateStore.h>

#include <span>
#include <vector>

class World;

// Whether the game keeps level state (SavedGames::KeepLevelState): docs/gameplay/saves.md, "Level entities".
[[nodiscard]] bool KeepsLevelState(const World& world);

// Alive entities carrying a persistent id, in `partition`.
[[nodiscard]] std::vector<EntityId> IdentifiedEntitiesIn(World& world, StoragePartitionId partition);

// Records how `identified`, all of `scope`'s zone, deviate from what it authored:
// docs/plans/runtime-stable-identity.md, "Zone state memory".
void CaptureZoneState(World& world, ZoneStateScope scope, std::span<const EntityId> identified, ZoneCapture capture);
