#pragma once

#include <ecs/EntityId.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <cstddef>
#include <vector>

// ─── PropagationOrderCache ────────────────────────────────────────────────────
//
// World resource that caches a parent-before-child propagation order for the
// transform hierarchy. Rebuilt lazily when Changed<Parent> detects that any
// entity gained or lost a Parent component (hierarchy structure changed).
//
// Each entry stores two EntityIds: the child and the parent (or an invalid id
// for root entities). The propagation sweep reads this list front-to-back,
// computing worlds[child] = worlds[parent] * locals[child], which requires that
// each parent entry has already been written before any of its children appear.
//
// Spiritual successor of TransformPropagationOrderService from the sparse-set
// era. See docs/ecs/decisions.md D3.1 for the mandate and benchmark rationale.

struct PropagationEntry
{
    EntityId Child;
    EntityId Parent; // invalid (Parent.IsValid() == false) for root entities
    LocalTransform* LocalPtr = nullptr;
    WorldTransform* WorldPtr = nullptr;
    const WorldTransform* ParentWorldPtr = nullptr;
};

class PropagationOrderCache
{
public:
    // Invalidates the cache so the next Propagate call rebuilds it.
    void Invalidate() { Dirty = true; }

    bool IsDirty() const { return Dirty; }
    bool ArchetypeCountMatches(size_t archetypeCount) const
    {
        return LastArchetypeCount == archetypeCount;
    }

    // Called by the propagation system after rebuilding.
    void MarkClean(size_t archetypeCount)
    {
        Dirty = false;
        LastArchetypeCount = archetypeCount;
    }

    std::vector<PropagationEntry>& GetOrder() { return Order; }
    const std::vector<PropagationEntry>& GetOrder() const { return Order; }

private:
    std::vector<PropagationEntry> Order;
    size_t LastArchetypeCount = 0;
    bool Dirty = true; // start dirty so the first frame always builds
};
