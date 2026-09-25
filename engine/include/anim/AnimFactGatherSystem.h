#pragma once

#include <anim/AnimFactProviders.h>
#include <anim/AnimFacts.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <ecs/Query.h>

#include <cstdint>
#include <optional>
#include <unordered_set>

class LoggingProvider;
class StoragePartitionSet;
struct FixedLogicContext;

// Fills every animated entity's facts each fixed tick: providers, then the tagset
// slot, then derivations in order. A rig that fails to bind or outgrows its storage
// leaves the entity untouched and is logged once per binding generation.
class AnimFactGatherSystem
{
public:
    explicit AnimFactGatherSystem(LoggingProvider* logging = nullptr) : Logging(logging) {}

    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world pass for tests and the preview; the schedule visits participating partitions.
    void Gather(World& world, AnimTick now, double tickSeconds);

private:
    void GatherImpl(World& world, const StoragePartitionSet* partitions, AnimTick now,
                    double tickSeconds);
    [[nodiscard]] const AnimBoundRig* Bind(AnimRigBindings& bindings, DataAssetHandle rig,
                                           const World& world);

    LoggingProvider* Logging = nullptr;

    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimFacts>, Write<AnimFactHistory>>> SmallQuery;
    std::optional<Query<Read<AnimRig>, Write<AnimFactsLarge>, Write<AnimFactHistory>>> LargeQuery;
    // Binding generations already logged for diagnostics and for storage mismatch.
    std::unordered_set<std::uint64_t> Reported;
    std::unordered_set<std::uint64_t> ReportedCapacity;
};

// Shared with the preview.
void GatherAnimFacts(const World& world,
                     EntityId entity,
                     const AnimBoundRig& rig,
                     std::span<std::uint32_t> values,
                     AnimFactHistory& history,
                     AnimTick now,
                     double tickSeconds);
