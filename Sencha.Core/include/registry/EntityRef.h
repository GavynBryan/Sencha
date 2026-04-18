#pragma once

#include <entity/EntityId.h>
#include <registry/RegistryId.h>

struct EntityRef
{
    RegistryId Registry;
    EntityId Entity;

    bool IsValid() const
    {
        return Registry.IsValid() && Entity.IsValid();
    }

    bool IsGlobal() const
    {
        return Registry.IsGlobal();
    }

    bool operator==(const EntityRef&) const = default;
};
