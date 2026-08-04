#pragma once

#include <assets/data/DataAssetCache.h>
#include <ecs/World.h>

struct FixedLogicContext;

class MovementTuningResolutionSystem
{
public:
    explicit MovementTuningResolutionSystem(DataAssetCache& dataAssets)
        : DataAssets(&dataAssets)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world overload for tests; the scheduled path visits only the
    // partitions participating this tick.
    void Step(World& world);

private:
    void StepImpl(World& world, const class StoragePartitionSet* partitions);

    DataAssetCache* DataAssets = nullptr;
};
