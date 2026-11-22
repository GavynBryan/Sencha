#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateStore.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class World;

// How one component's runtime state is remembered while its zone is away and
// written to a save: a versioned byte form its owner defines.
struct ZoneComponentMemory
{
    ComponentTypeId Type;
    std::uint16_t Version = 1;
    // Leaves `out` empty when the component is as authored.
    void (*Capture)(const World& world, EntityId entity, std::vector<std::byte>& out) = nullptr;
    // Given a record of `version`, which is never newer than Version.
    void (*Restore)(World& world, EntityId entity, std::uint16_t version, std::span<const std::byte> bytes) = nullptr;
};

// World resource.
class ZoneComponentMemories
{
public:
    // A second registration for a type replaces the first.
    void Register(const ZoneComponentMemory& memory);
    [[nodiscard]] std::span<const ZoneComponentMemory> All() const { return Memories; }

private:
    std::vector<ZoneComponentMemory> Memories;
};

// Alive entities carrying a persistent id, in `partition`.
[[nodiscard]] std::vector<EntityId> IdentifiedEntitiesIn(World& world, StoragePartitionId partition);

// Records how `identified`, all of `scope`'s zone, deviate from what it authored.
void CaptureZoneState(World& world, ZoneStateScope scope, std::span<const EntityId> identified, ZoneCapture capture);

// Gives `created`, just imported under `scope`, the component state remembered for them.
void RestoreZoneState(World& world, ZoneStateScope scope, std::span<const EntityId> created);
