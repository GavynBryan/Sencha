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

//=============================================================================
// AnimFactGatherSystem
//
// Once per fixed tick, fills every animated entity's fact snapshot: gathered
// slots from this World's providers, a tagset slot from the entity's tag
// container, then the rig's derivations in order. An entity whose rig does not
// bind, or whose storage is smaller than its rig's layout, is left untouched,
// and the reason is logged once per binding generation rather than once per
// entity per tick.
//
// Rigs resolve through the World's AnimRigBindings resource, which the host
// publishes; a World without one gathers nothing.
//=============================================================================
class AnimFactGatherSystem
{
public:
    explicit AnimFactGatherSystem(LoggingProvider* logging = nullptr) : Logging(logging) {}

    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world pass for tests and the preview; the scheduled path visits
    // only the partitions participating this tick.
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
    // Binding generations whose diagnostics, and whose storage mismatch, have
    // been logged.
    std::unordered_set<std::uint64_t> Reported;
    std::unordered_set<std::uint64_t> ReportedCapacity;
};

// Fills one entity's snapshot. The pure half, shared with the preview.
void GatherAnimFacts(const World& world,
                     EntityId entity,
                     const AnimBoundRig& rig,
                     std::span<std::uint32_t> values,
                     AnimFactHistory& history,
                     AnimTick now,
                     double tickSeconds);
