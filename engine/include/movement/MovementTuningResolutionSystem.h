#pragma once

#include <assets/data/DataAssetCache.h>
#include <ecs/World.h>

class LoggingProvider;
struct FixedLogicContext;

class MovementTuningResolutionSystem
{
public:
    // `logging` is where a profile that fails to bind is reported. Optional so
    // a focused test can resolve tuning without one, but a host should pass it:
    // an unreported bind failure looks exactly like tuning that was authored
    // wrong.
    explicit MovementTuningResolutionSystem(DataAssetCache& dataAssets,
                                            LoggingProvider* logging = nullptr)
        : DataAssets(&dataAssets)
        , Logging(logging)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world overload for tests; the scheduled path visits only the
    // partitions participating this tick.
    void Step(World& world);

private:
    void StepImpl(World& world, const class StoragePartitionSet* partitions);

    DataAssetCache* DataAssets = nullptr;
    LoggingProvider* Logging = nullptr;
};
