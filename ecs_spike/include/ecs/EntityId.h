#pragma once

#include <cstdint>

// Generational entity handle.
// Index identifies a slot; Generation distinguishes slot reuses.
// Storage reads EntityIndex only; generation is checked at API boundaries.
using EntityIndex = uint32_t;
constexpr EntityIndex InvalidEntityIndex = UINT32_MAX;

struct EntityId
{
    EntityIndex Index      = InvalidEntityIndex;
    uint32_t    Generation = 0;

    bool IsValid() const { return Index != InvalidEntityIndex; }
    bool operator==(const EntityId&) const = default;
};
