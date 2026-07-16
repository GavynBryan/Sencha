#pragma once

#include <cassert>
#include <cstdint>

#include <core/ResourceStore.h>
#include <ecs/World.h>
#include <world/registry/RegistryId.h>
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
        Components.AdvanceFrame();
    }

    Registry(RegistryId id, RegistryKind kind, ZoneId zone = {})
        : Id(id)
        , Kind(kind)
        , Zone(zone)
    {
        assert((kind != RegistryKind::Global
                || (id.IsGlobal() && !zone.IsValid()))
               && "Global registry requires the global id and no zone id");
        assert((kind != RegistryKind::Zone
                || (id.IsValid() && !id.IsGlobal() && zone.IsValid()))
               && "Zone registry requires a non-global id and a valid zone id");

        Components.AdvanceFrame();
    }

    ~Registry()
    {
        Components.ClearEntities();
    }

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    RegistryId Id;
    RegistryKind Kind = RegistryKind::Transient;
    ZoneId Zone;

    ResourceStore Resources;
    World Components;
};
