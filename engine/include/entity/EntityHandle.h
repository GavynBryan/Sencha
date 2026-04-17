#pragma once

#include <cstdint>

using EntityId = uint32_t;
constexpr EntityId InvalidEntityId = UINT32_MAX;

struct EntityHandle 
{
    EntityId Id;
    uint16_t Generation;

    bool IsValid() const { return Id != InvalidEntityId; }
};