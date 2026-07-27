#pragma once

#include <cstdint>
#include <cassert>
#include <ecs/World.h>
#include <world/registry/RegistryId.h>
#include <world/ResourceRegistry.h>
#include <zone/ZoneId.h>

enum class RegistryKind : uint8_t
{
    Global,
    Zone,
    Transient,
};

struct Registry
{
    Registry() = default;

    Registry(Registry&&) = default;
    Registry& operator=(Registry&&) = default;

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    RegistryId Id;
    RegistryKind Kind = RegistryKind::Zone;
    ZoneId Zone;

    // Archetype ECS storage for this registry. The field name is kept during
    // the migration because much engine code already talks about a registry's
    // "components"; the type is now the ECS World, not a sparse-set registry.
    World Components;
    ResourceRegistry Resources;
};
