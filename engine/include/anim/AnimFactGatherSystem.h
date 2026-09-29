#pragma once

#include <anim/AnimClock.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimFacts.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <ecs/Query.h>

#include <cstdint>
#include <optional>

class StoragePartitionSet;
struct FixedLogicContext;

// Fills every animated entity's facts each fixed tick: providers, then the tagset
// slot, then derivations in order. Which entities carry facts, and whether they keep
// derivation memory, is AnimRigCompositionSystem's to decide.
class AnimFactGatherSystem
{
public:
    explicit AnimFactGatherSystem(bool presentsPose = true)
        : PresentsPose(presentsPose)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world pass for tests and the preview; the schedule visits participating partitions.
    void Gather(World& world, AnimTick now, double tickSeconds);

private:
    void GatherImpl(World& world, const StoragePartitionSet* partitions, const AnimClock& clock,
                    double tickSeconds);

    bool PresentsPose = true;

    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimFacts>, Write<AnimFactHistory>>> SmallKept;
    std::optional<Query<Read<AnimRig>, Write<AnimFacts>, Without<AnimFactHistory>>> Small;
    std::optional<Query<Read<AnimRig>, Write<AnimFactsLarge>, Write<AnimFactHistory>>> LargeKept;
    std::optional<Query<Read<AnimRig>, Write<AnimFactsLarge>, Without<AnimFactHistory>>> Large;
};

// Shared with the preview. `history` is null for a rig whose derivations keep no memory.
void GatherAnimFacts(const World& world,
                     EntityId entity,
                     const AnimBoundRig& rig,
                     std::span<std::uint32_t> values,
                     AnimFactHistory* history,
                     AnimTick now,
                     double tickSeconds);
