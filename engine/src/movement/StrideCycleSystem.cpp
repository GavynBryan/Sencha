#include <movement/StrideCycleSystem.h>

#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <algorithm>
#include <cmath>

void AdvanceStrideCycle(StrideCycle& stride, const Vec3d& velocity, const Vec3d& surfaceVelocity,
                        const Vec3d& up, bool supported, float strideLength, float seconds)
{
    stride.PreviousPosition = stride.Position;
    stride.PreviousGroundSpeed = stride.GroundSpeed;

    float speed = 0.0f;
    if (supported && strideLength > 0.0f)
    {
        const Vec3d relative = velocity - surfaceVelocity;
        const float upLength = up.Magnitude();
        const Vec3d axis = upLength > 1.0e-6f ? up / upLength : Vec3d::Up();
        speed = (relative - axis * relative.Dot(axis)).Magnitude();
    }
    stride.GroundSpeed = std::isfinite(speed) ? speed : 0.0f;

    if (!(seconds > 0.0f) || stride.GroundSpeed <= 0.0f)
        return;
    const float advance = std::min(stride.GroundSpeed * seconds / strideLength, kStrideCycleMaxAdvance);
    stride.Position += advance;
    if (stride.Position >= 1.0f)
        stride.Position -= 1.0f;
}

void StrideCycleSystem::PostFixed(PostFixedContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions, static_cast<float>(ctx.Time.DeltaSeconds));
}

void StrideCycleSystem::Step(World& world, float tickSeconds)
{
    StepImpl(world, nullptr, tickSeconds);
}

void StrideCycleSystem::StepImpl(World& world, const StoragePartitionSet* partitions, float tickSeconds)
{
    if (!world.IsRegistered<StrideCycle>() || !world.IsRegistered<KinematicState>()
        || !world.IsRegistered<SupportState>() || !world.IsRegistered<MotionRequest>()
        || !world.IsRegistered<ResolvedMovementTuning>())
    {
        return;
    }
    if (QueriedWorld != &world)
    {
        Strides.reset();
        QueriedWorld = &world;
    }
    if (!Strides.has_value())
        Strides.emplace(world);

    const auto visit = [tickSeconds](auto& view)
    {
        auto strides = view.template Write<StrideCycle>();
        const auto kinematics = view.template Read<KinematicState>();
        const auto supports = view.template Read<SupportState>();
        const auto requests = view.template Read<MotionRequest>();
        const auto tunings = view.template Read<ResolvedMovementTuning>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            AdvanceStrideCycle(strides[i], kinematics[i].Velocity, supports[i].SurfaceVelocity, requests[i].UpAxis,
                               supports[i].Kind == SupportKind::Stable, tunings[i].StrideLength, tickSeconds);
        }
    };
    if (partitions != nullptr)
        Strides->ForEachChunkIn(*partitions, visit);
    else
        Strides->ForEachChunk(visit);
}
