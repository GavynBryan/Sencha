#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <vector>

class World;
struct FixedLogicContext;
struct MotionAxisOverride;

//=============================================================================
// RootMotionSource
//
// A character's motion for one tick decided somewhere other than movement --
// an animation carrying the character through a mantle -- and asked for as a
// pure function of the tick, so a replayed tick gets exactly the answer the
// live tick did. Movement asks; it never knows what answered.
//
// The source is a World resource the owning layer installs (animation
// installs its clip sampler). A World without one has no root motion.
//=============================================================================

struct RootMotionSample
{
    // Across the ground, in world space, relative to the support.
    Vec3d PlanarVelocity = Vec3d::Zero();
    // About the up axis.
    float TurnRadians = 0.0f;
};

struct RootMotionSource
{
    // True, with `out` filled, when `entity` is being carried on `tick`.
    // Ticks are in the numbering the source keeps its time in -- the
    // authority's -- and `tickSeconds` is the fixed step.
    using SampleFn = bool (*)(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                              RootMotionSample& out);
    SampleFn Sample = nullptr;
};

// The World's source's answer for `entity` on `tick`. False when there is no
// source or nothing carries the entity.
[[nodiscard]] bool SampleRootMotion(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                                    RootMotionSample& out);

// A sample in a character's channels: it replaces the planar channel -- the
// character goes where it is carried, whatever it was asked to walk -- and
// turns it. The up channel stays the locomotion mode's, so gravity and
// jumping still hold.
void ApplyRootMotion(MotionAxisOverride& overrides, const RootMotionSample& sample);

// Asks the source for every character each fixed tick, between the action
// producers and composition.
class RootMotionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    // Whole-world overload for tests.
    void Step(World& world, std::uint64_t tick, double tickSeconds);

private:
    void StepImpl(World& world, const class StoragePartitionSet* partitions, std::uint64_t tick, double tickSeconds);

    // Reused each tick.
    std::vector<EntityId> Characters;
};
