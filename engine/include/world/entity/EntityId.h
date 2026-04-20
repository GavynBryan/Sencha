#pragma once

#include <cstdint>

using EntityIndex = uint32_t;
constexpr EntityIndex InvalidEntityIndex = UINT32_MAX;

struct EntityId
{
    EntityIndex Index = InvalidEntityIndex;
    uint16_t Generation = 0;

    bool IsValid() const
    {
        return Index != InvalidEntityIndex;
    }

    bool operator==(const EntityId&) const = default;
};
