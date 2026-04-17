#pragma once

#include <cstdint>

using EntityId = uint32_t;
constexpr EntityId InvalidEntityId = UINT32_MAX;

struct EntityHandle 
{
    EntityId Id = InvalidEntityId;
    uint16_t Generation = 0;

    bool IsValid() const { return Id != InvalidEntityId; }
    bool operator==(const EntityHandle&) const = default;
};
