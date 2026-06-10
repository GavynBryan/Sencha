#pragma once

#include <ecs/Chunk.h>
#include <ecs/EntityId.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <cstddef>
#include <vector>

// ─── PropagationOrderCache ────────────────────────────────────────────────────
//
// World resource that caches a parent-before-child propagation order for the
// transform hierarchy. Rebuilt lazily when either:
//   - Changed<Parent> detects a hierarchy edit (parent added/removed/retargeted), or
//   - World::StructuralVersion() differs from the version seen at last rebuild
//     (any entity create/destroy or component add/remove can move rows and
//     stale every cached pointer below — archetype count alone is NOT a
//     sufficient key; see docs/ecs/decisions.md D4.4).
//
// Each entry stores the child and parent EntityIds (parent invalid for roots)
// plus pointers resolved at rebuild time so the steady-state sweep does no
// registry lookups. ChunkPtr/LocalCol/WorldCol let the sweep read the
// LocalTransform column version (skip clean subtrees) and bump the
// WorldTransform column version for exactly the chunks it writes.
// LocalTransform and WorldTransform of one entity always share a chunk, so a
// single chunk pointer serves both columns.
//
// ParentOrderIndex points at the parent's entry within Order (parents always
// precede children), letting the sweep propagate dirtiness down the tree with
// one flag array and no hashing. UINT32_MAX for roots.
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
    Chunk*   ChunkPtr = nullptr;          // chunk holding both transform columns
    uint32_t LocalCol = UINT32_MAX;       // LocalTransform column in ChunkPtr
    uint32_t WorldCol = UINT32_MAX;       // WorldTransform column in ChunkPtr
    uint32_t ParentOrderIndex = UINT32_MAX; // parent's index in Order; UINT32_MAX for roots
};

class PropagationOrderCache
{
public:
    // Invalidates the cache so the next Propagate call rebuilds it.
    void Invalidate() { Dirty = true; }

    bool IsDirty() const { return Dirty; }

    bool StructuralVersionMatches(uint64_t structuralVersion) const
    {
        return LastStructuralVersion == structuralVersion;
    }

    // Called by the propagation system after rebuilding. A rebuild always
    // forces the next sweep to recompute every entry: structural moves copy
    // component data without bumping column versions, so change detection
    // alone cannot be trusted across a rebuild.
    void MarkClean(uint64_t structuralVersion)
    {
        Dirty = false;
        LastStructuralVersion = structuralVersion;
        FullSweepPending = true;
    }

    // True exactly once after each rebuild; consumed by the sweep.
    bool ConsumeFullSweepPending()
    {
        const bool pending = FullSweepPending;
        FullSweepPending = false;
        return pending;
    }

    // Frame of the last completed sweep. The sweep treats an entry as locally
    // dirty when its LocalTransform column version >= this value (>= rather
    // than >, so writes landing later in the same frame as a sweep are not
    // missed; the cost is one redundant re-propagation per write).
    uint32_t LastSweepFrame() const { return SweepFrame; }
    void SetLastSweepFrame(uint32_t frame) { SweepFrame = frame; }

    std::vector<PropagationEntry>& GetOrder() { return Order; }
    const std::vector<PropagationEntry>& GetOrder() const { return Order; }

    // Frame-local dirty flags, indexed like Order. Owned here so the sweep
    // reuses one allocation across frames.
    std::vector<uint8_t>& DirtyScratch() { return Scratch; }

private:
    std::vector<PropagationEntry> Order;
    std::vector<uint8_t> Scratch;
    uint64_t LastStructuralVersion = 0;
    uint32_t SweepFrame = 0;
    bool FullSweepPending = true;
    bool Dirty = true; // start dirty so the first frame always builds
};
