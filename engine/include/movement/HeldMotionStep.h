#pragma once

#include <ecs/EntityId.h>
#include <ecs/Query.h>
#include <math/Vec.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/HeldMotion.h>
#include <time/ActorTime.h>

#include <optional>
#include <vector>

class StoragePartitionSet;
class World;
struct FixedLogicContext;

// A character's held motion over one step of `dt` actor seconds, forced into
// `overrides` over what locomotion, actions and root motion asked for: the live
// bursts' velocities summed, then a taut tether's pull, never past taut in a step. A burst whose time is
// out, a tether whose anchor is gone and any session whose holder is gone end.
// The live tick and replay both step held motion through this one function.
void StepHeldMotion(World& world, EntityId entity, MotionAxisOverride& overrides, float dt, Vec3d upAxis);

// Whether anything holds the character's motion now.
[[nodiscard]] bool HoldsMotion(const MotionBursts& bursts, const TetherSession& tether);

// Steps the held motion of every undilated character after root motion and
// before composition, and frees a leased mode whose holder is gone.
class HeldMotionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    // Whole-world overload for tests.
    void Step(World& world, float dt);

private:
    void StepImpl(World& world, const StoragePartitionSet* partitions, float dt);

    std::vector<EntityId> Held;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<MotionBursts>, Read<TetherSession>, Read<LocomotionModeLease>, Without<ActorTimeDilated>>>
        Characters;
};
