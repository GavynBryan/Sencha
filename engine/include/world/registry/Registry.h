#pragma once

#include <cassert>
#include <cstdint>
#include <utility>

#include <ecs/World.h>
#include <world/registry/RegistryId.h>
#include <core/ResourceStore.h>
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
    Registry()
    {
        // Column version 0 is the "never written" sentinel used by Changed<T>.
        // Runtime registries therefore begin at epoch 1 so components created
        // during initial or detached-zone construction record a real write.
        Components.AdvanceFrame();
    }

    ~Registry()
    {
        // Run component teardown while both registry resource owners still live.
        Components.ClearEntities();
    }

    Registry(Registry&& other) noexcept
        : Id(other.Id)
        , Kind(other.Kind)
        , Zone(other.Zone)
        , Components(std::move(other.Components))
        , Resources(std::move(other.Resources))
    {
    }

    Registry& operator=(Registry&& other) noexcept
    {
        Id = other.Id;
        Kind = other.Kind;
        Zone = other.Zone;
        Components = std::move(other.Components);
        Resources = std::move(other.Resources);
        return *this;
    }

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    RegistryId Id;
    RegistryKind Kind = RegistryKind::Zone;
    ZoneId Zone;

    World Components;
    ResourceStore Resources;
};

inline Registry MakeGlobalRegistry(RegistryId id = RegistryId::Global())
{
    assert(id.IsValid() && "MakeGlobalRegistry: registry id must be valid");
    assert(id.IsGlobal() && "MakeGlobalRegistry: global registry id must be RegistryId::Global");

    Registry registry;
    registry.Id = id;
    registry.Kind = RegistryKind::Global;
    registry.Zone = ZoneId{};

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
