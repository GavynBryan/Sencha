#pragma once

#include <cstdint>
#include <entity/EntityRegistry.h>
#include <registry/RegistryId.h>
#include <world/ComponentRegistry.h>

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

    EntityRegistry Entities;
    ComponentRegistry Components;
};
