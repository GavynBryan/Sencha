#pragma once

#include <ecs/Chunk.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionSet.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>
#include <cstdint>
#include <vector>

enum class TransformPropagationDomain : std::uint8_t
{
    Simulation,
    Presentation,
};

struct PropagationEntry
{
    EntityId Child;
    EntityId Parent;
    LocalTransform* LocalPtr = nullptr;
    WorldTransform* WorldPtr = nullptr;
    const WorldTransform* ParentWorldPtr = nullptr;
    Chunk* ChunkPtr = nullptr;
    uint32_t LocalCol = UINT32_MAX;
    uint32_t WorldCol = UINT32_MAX;
    uint32_t ParentOrderIndex = UINT32_MAX;
};

// One sweep state per cadence/domain. Simulation and presentation use different
// partition sets, so sharing one last-sweep frame would make a zone that slept
// through one domain miss parent changes when it re-entered that domain.
struct PropagationSweepState
{
    StoragePartitionSet PreviousPartitions;
    uint32_t LastSweepFrame = 0;
};

class PropagationOrderCache
{
public:
    void Invalidate() { Dirty = true; }
    bool IsDirty() const { return Dirty; }

    bool StructuralVersionMatches(uint64_t structuralVersion) const
    {
        return LastStructuralVersion == structuralVersion;
    }

    void MarkClean(uint64_t structuralVersion)
    {
        Dirty = false;
        LastStructuralVersion = structuralVersion;
        FullSweepPending = true;
    }

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

private:
    std::vector<PropagationEntry> Order;
    std::vector<uint8_t> Scratch;
    uint64_t LastStructuralVersion = 0;
    PropagationSweepState SimulationSweep;
    PropagationSweepState PresentationSweep;
    bool FullSweepPending = true;
    bool Dirty = true;
};
