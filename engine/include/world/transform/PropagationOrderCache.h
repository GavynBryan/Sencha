#pragma once

#include <ecs/Chunk.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionSet.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

enum class TransformPropagationDomain : std::uint8_t
{
    Simulation,
    Presentation,
};

// One parented transform entity, held in parent-before-child order.
//
// Only entities carrying Parent appear here. An unparented entity's world
// transform is its local transform, so it needs no ordering and is swept
// chunk-linearly by a filtered query instead. Keeping those out of the order is
// what makes both the order and its maintenance proportional to the hierarchy
// rather than to everything resident.
struct PropagationEntry
{
    EntityId Child;
    EntityId Parent;
    LocalTransform* LocalPtr = nullptr;
    WorldTransform* WorldPtr = nullptr;
    const WorldTransform* ParentWorldPtr = nullptr;
    Chunk* ChunkPtr = nullptr;
    // Set only when the parent is not itself in the order — an unparented root,
    // swept by the flat pass. Its world column's last-written frame is how this
    // entry learns the root moved; a parent inside the order reports that
    // through the sweep's dirty flags instead.
    Chunk* ParentChunkPtr = nullptr;
    uint32_t LocalCol = UINT32_MAX;
    uint32_t WorldCol = UINT32_MAX;
    uint32_t ParentWorldCol = UINT32_MAX;
    uint32_t ParentOrderIndex = UINT32_MAX;
};

// One sweep state per cadence/domain. Simulation and presentation use different
// partition sets, so sharing one last-sweep frame would make a zone that slept
// through one domain miss parent changes when it re-entered that domain.
struct PropagationSweepState
{
    StoragePartitionSet PreviousPartitions;
    uint32_t LastSweepFrame = 0;
    bool Swept = false;
};

// Persistent state for transform propagation: the parent-before-child order over
// parented entities, and where those entities' rows currently live.
//
// The two have separate validity because different things invalidate them:
//
//   topology   who is whose parent, and in what order. Changes only when the
//              hierarchy changes, so a spawn, despawn, or zone attach that
//              touches no Parent column leaves it standing. This is the property
//              that keeps a streaming event from costing a rebuild proportional
//              to everything resident.
//   addresses  where those entities' rows are. Any structural change can
//              relocate a row, because removal is a swap-remove, so these are
//              re-resolved whenever the structural version moves. That work is
//              proportional to the order, not to the world.
//
// Conflating the two is what made every spawn anywhere rebuild a world-global
// order; see docs/plans/unified-world-hardening.md.
class PropagationOrderCache
{
public:
    // Forces a topology rebuild, and with it an address resolve and one full
    // sweep. The propagation bisect switch and the ECS benchmark use this.
    void Invalidate() { TopologyDirty = true; }
    bool IsDirty() const { return TopologyDirty; }

    // ── Topology ─────────────────────────────────────────────────────────────

    // parentCount is the number of entities carrying Parent. It is a superset of
    // the order's entries — an entity can carry Parent without carrying
    // transforms — and it is deliberately the coarser signal: it catches Parent
    // components appearing and disappearing, including through entity
    // destruction, which no column-version probe reports.
    bool TopologyMatches(std::size_t parentCount) const
    {
        return !TopologyDirty && LastParentCount == parentCount;
    }

    uint32_t TopologyFrame() const { return LastTopologyFrame; }

    void MarkTopologyClean(std::size_t parentCount, uint32_t frame)
    {
        TopologyDirty = false;
        LastParentCount = parentCount;
        LastTopologyFrame = frame;
        AddressesDirty = true;
        FullSweepPending = true;
        ++RebuildCounter;
    }

    // Order rebuilds since construction. This is the invalidation blast-radius
    // signal: work unrelated to a changed partition pays for a bump here. Tests
    // bound it; the bench reports it.
    uint64_t RebuildCount() const { return RebuildCounter; }

    // ── Row addresses ────────────────────────────────────────────────────────

    bool AddressesMatch(uint64_t structuralVersion) const
    {
        return !AddressesDirty && LastStructuralVersion == structuralVersion;
    }

    void MarkAddressesResolved(uint64_t structuralVersion)
    {
        AddressesDirty = false;
        LastStructuralVersion = structuralVersion;
        ++AddressResolveCounter;
    }

    // Address re-resolutions since construction. Reported separately from
    // RebuildCount because the two have different causes and very different
    // costs, and because how often a live scene pays this is the measurement
    // that decides whether caching row pointers is worth its invalidation.
    uint64_t AddressResolveCount() const { return AddressResolveCounter; }

    // ── Sweep ────────────────────────────────────────────────────────────────

    bool ConsumeFullSweepPending()
    {
        const bool pending = FullSweepPending;
        FullSweepPending = false;
        return pending;
    }

    PropagationSweepState& SweepState(TransformPropagationDomain domain)
    {
        return domain == TransformPropagationDomain::Simulation
            ? SimulationSweep
            : PresentationSweep;
    }

    std::vector<PropagationEntry>& GetOrder() { return Order; }
    const std::vector<PropagationEntry>& GetOrder() const { return Order; }
    std::vector<uint8_t>& DirtyScratch() { return Scratch; }

    // Partitions that entered the swept set since the previous sweep of this
    // domain. Retained rather than local so a per-frame sweep allocates nothing.
    StoragePartitionSet& EnteringScratch() { return Entering; }

    // Retained rebuild workspace: the parented entities, each one's parent slot,
    // and their children in compressed adjacency form. Held on the cache purely
    // so that a rebuild after the first allocates nothing.
    struct RebuildWorkspace
    {
        std::vector<EntityId> Child;
        std::vector<EntityId> Parent;
        std::vector<uint32_t> ParentSlot;
        std::vector<uint32_t> ChildOffset;
        std::vector<uint32_t> ChildCursor;
        std::vector<uint32_t> ChildSlots;
        std::vector<uint32_t> OrderSlot;
        std::unordered_map<EntityIndex, uint32_t> SlotOfIndex;
    };

    RebuildWorkspace& Workspace() { return Rebuild; }

private:
    std::vector<PropagationEntry> Order;
    std::vector<uint8_t> Scratch;
    StoragePartitionSet Entering;
    RebuildWorkspace Rebuild;
    std::size_t LastParentCount = 0;
    uint32_t LastTopologyFrame = 0;
    uint64_t LastStructuralVersion = 0;
    uint64_t RebuildCounter = 0;
    uint64_t AddressResolveCounter = 0;
    PropagationSweepState SimulationSweep;
    PropagationSweepState PresentationSweep;
    bool FullSweepPending = true;
    bool TopologyDirty = true;
    bool AddressesDirty = true;
};
