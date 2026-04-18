#pragma once

#include <cstdint>
#include <cassert>
#include <world/entity/EntityRegistry.h>
#include <world/registry/RegistryId.h>
#include <world/ComponentRegistry.h>
#include <zone/ZoneId.h>

enum class RegistryKind : uint8_t
{
    Global,
    Zone,
    Transient,
    Boundary
};

struct Registry
{
    RegistryId Id;
    RegistryKind Kind = RegistryKind::Zone;
    ZoneId Zone;

    EntityRegistry Entities;
    ComponentRegistry Components;
};

inline Registry MakeGlobalRegistry(RegistryId id = RegistryId::Global())
{
    assert(id.IsValid() && "MakeGlobalRegistry: registry id must be valid");
    assert(id.IsGlobal() && "MakeGlobalRegistry: global registry id must be RegistryId::Global");

    Registry registry;
    registry.Id = id;
    registry.Kind = RegistryKind::Global;
    registry.Zone = ZoneId::Invalid();

    assert(!registry.Zone.IsValid() && "Global registry must not have a ZoneId");
    return registry;
}

inline Registry MakeZoneRegistry(RegistryId id, ZoneId zone)
{
    assert(id.IsValid() && "MakeZoneRegistry: registry id must be valid");
    assert(zone.IsValid() && "Zone registry must have a valid ZoneId");

    Registry registry;
    registry.Id = id;
    registry.Kind = RegistryKind::Zone;
    registry.Zone = zone;

    assert(registry.Zone.IsValid() && "Zone registry must have a valid ZoneId");
    return registry;
}
