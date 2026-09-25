#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <vector>

class World;
struct FixedLogicContext;
struct MotionAxisOverride;

// A World resource another layer installs, asked for a character's motion as a
// pure function of the tick so a replay gets the live answer
// (docs/gameplay/movement.md, "Root motion").

struct RootMotionSample
{
    // Across the ground, in world space, relative to the support.
    Vec3d PlanarVelocity = Vec3d::Zero();
    // About the up axis.
    float TurnRadians = 0.0f;
};

struct RootMotionSource
{
    // True, with `out` filled, when `entity` is carried on `tick`, which is in
    // the authority's numbering.
    using SampleFn = bool (*)(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                              RootMotionSample& out);
    SampleFn Sample = nullptr;
};

// False when there is no source or nothing carries the entity.
[[nodiscard]] bool SampleRootMotion(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                                    RootMotionSample& out);

// Force-replaces the planar channel and sets the turn. The up channel stays the
// mode's, so gravity and jumping still hold.
void ApplyRootMotion(MotionAxisOverride& overrides, const RootMotionSample& sample);

// Runs between the action producers and composition.
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
