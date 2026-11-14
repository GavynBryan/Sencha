#pragma once

#include <components/StrideCycle.h>
#include <ecs/Query.h>
#include <math/Vec.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>

#include <optional>

class StoragePartitionSet;
class World;
struct PostFixedContext;

// The most of a stride one tick may advance: beyond half, the short way round
// the wrap would read the cycle as running backwards.
inline constexpr float kStrideCycleMaxAdvance = 0.45f;

// One tick of one character: `velocity` across the ground at `surfaceVelocity`,
// measured perpendicular to `up`. A character off stable ground, or with a stride
// length of zero, keeps its place in the cycle and reports no ground speed.
void AdvanceStrideCycle(StrideCycle& stride, const Vec3d& velocity, const Vec3d& surfaceVelocity,
                        const Vec3d& up, bool supported, float strideLength, float seconds);

// Counts every character's stride after the tick's movement, from the motion the
// motor achieved, the ground it ended on and the tick's resolved stride length.
class StrideCycleSystem
{
public:
    void PostFixed(PostFixedContext& ctx);

    // Whole-world overload for tests.
    void Step(World& world, float tickSeconds);

private:
    using StrideQuery = Query<Write<StrideCycle>, Read<KinematicState>, Read<SupportState>, Read<MotionRequest>,
                              Read<ResolvedMovementTuning>>;

    void StepImpl(World& world, const StoragePartitionSet* partitions, float tickSeconds);

    std::optional<StrideQuery> Strides;
    const World* QueriedWorld = nullptr;
};
