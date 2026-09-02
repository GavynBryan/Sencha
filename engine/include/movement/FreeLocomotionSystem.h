#pragma once

#include <ecs/World.h>
#include <math/Vec.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>

struct FixedLogicContext;
struct MovementIntent;

[[nodiscard]] LocomotionOutput StepFreeLocomotion(const KinematicState& kinematic,
                                                  const SupportState& support,
                                                  const MovementIntent& intent,
                                                  const ResolvedMovementTuning& tuning,
                                                  Vec3d gravity,
                                                  Vec3d upAxis,
                                                  float dt);

class FreeLocomotionSystem
{
public:
    explicit FreeLocomotionSystem(Vec3d gravity = Vec3d(0.0f, -9.81f, 0.0f),
                                  Vec3d upAxis = Vec3d(0.0f, 1.0f, 0.0f))
        : Gravity(gravity)
        , UpAxis(upAxis)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

    // Whole-world overload for tests; the scheduled path visits only the
    // partitions participating this tick.
    void Step(World& world, float dt);

    // The values every scheduled locomotion step runs under. Prediction replay
    // reads them from here rather than restating them, so a re-run tick cannot
    // quietly integrate under different physics than the tick it re-runs.
    [[nodiscard]] Vec3d GetGravity() const { return Gravity; }
    [[nodiscard]] Vec3d GetUpAxis() const { return UpAxis; }

private:
    void StepImpl(World& world, const class StoragePartitionSet* partitions, float dt);

    Vec3d Gravity;
    Vec3d UpAxis;
};
