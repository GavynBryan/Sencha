#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

class World;
struct FixedLogicContext;
struct LocomotionOutput;
struct MotionAxisOverride;
struct MotionImpulse;
struct MotionRequest;
struct SupportState;

// Ordinary action producers are first-write-wins per channel. Forced reactions
// explicitly replace a prior producer. Up velocity is support-relative along the
// current locomotion up axis; planar velocity is projected into its perpendicular
// plane. Support motion is added after those controllable relative channels, so a
// jump from a rising platform inherits the platform's velocity.
[[nodiscard]] bool TrySetPlanarMotionOverride(World& world, EntityId entity, Vec3d velocity);
[[nodiscard]] bool TrySetUpMotionOverride(World& world, EntityId entity, float velocity);
[[nodiscard]] bool ForceSetPlanarMotionOverride(World& world, EntityId entity, Vec3d velocity);
[[nodiscard]] bool ForceSetUpMotionOverride(World& world, EntityId entity, float velocity);
[[nodiscard]] bool AddMotionImpulse(World& world, EntityId entity, Vec3d deltaVelocity);

[[nodiscard]] MotionRequest ComposeMotion(const LocomotionOutput& locomotion,
                                          const SupportState* support,
                                          const MotionAxisOverride& overrides,
                                          const MotionImpulse& impulses);

class MotionCompositionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world overload for tests; the scheduled path visits only the
    // partitions participating this tick.
    void Step(World& world);

private:
    void StepImpl(World& world, const class StoragePartitionSet* partitions);
};
