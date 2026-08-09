#pragma once

#include <cstddef>
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

// For the unordered containers that key on an entity. Index and generation are
// folded together rather than hashing the index alone, because a recycled slot
// is a different entity and must not collide with the one it replaced.
struct EntityIdHash
{
    [[nodiscard]] std::size_t operator()(EntityId entity) const
    {
        return (static_cast<std::size_t>(entity.Index) << 32)
             ^ static_cast<std::size_t>(entity.Generation);
    }
};
