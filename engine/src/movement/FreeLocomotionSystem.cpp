#include <movement/FreeLocomotionSystem.h>

#include <app/GameContexts.h>
#include <movement/MovementIntent.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementStep.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    constexpr float kMinimumAxisLengthSquared = 1.0e-8f;

    Vec3d SafeUpAxis(const Vec3d& candidate)
    {
        if (candidate.SqrMagnitude() <= kMinimumAxisLengthSquared)
            return Vec3d(0.0f, 1.0f, 0.0f);
        return candidate.Normalized();
    }

    Vec3d ProjectOntoPlane(const Vec3d& value, const Vec3d& unitNormal)
    {
        return value - unitNormal * value.Dot(unitNormal);
    }
}

LocomotionOutput StepFreeLocomotion(const KinematicState& kinematic,
                                    const SupportState& support,
                                    const MovementIntent& intent,
                                    const ResolvedMovementTuning& tuning,
                                    Vec3d gravity,
                                    Vec3d upAxis,
                                    float dt)
{
    LocomotionOutput output;
    output.UpAxis = SafeUpAxis(upAxis);
    output.GravityScale = std::isfinite(tuning.GravityScale) ? tuning.GravityScale : 1.0f;

    if (!(dt > 0.0f) || !std::isfinite(dt))
    {
        output.Velocity = support.Kind == SupportKind::Stable
            ? kinematic.Velocity - support.SurfaceVelocity
            : kinematic.Velocity;
        return output;
    }

    Vec3d relativeVelocity = support.Kind == SupportKind::Stable
        ? kinematic.Velocity - support.SurfaceVelocity
        : kinematic.Velocity;

    Vec3d planarVelocity = ProjectOntoPlane(relativeVelocity, output.UpAxis);
    float upVelocity = relativeVelocity.Dot(output.UpAxis);

    if (tuning.Friction > 0.0f)
    {
        movement::ApplyFriction(
            planarVelocity,
            tuning.Friction,
            std::max(0.0f, tuning.StopSpeed),
            dt);
    }

    Vec3d planarWish = ProjectOntoPlane(intent.WishDir, output.UpAxis);
    const float wishMagnitude = planarWish.Magnitude();
    if (wishMagnitude > movement::kEpsilon)
    {
        const Vec3d wishDirection = planarWish * (1.0f / wishMagnitude);
        const float inputStrength = std::clamp(wishMagnitude, 0.0f, 1.0f);
        const float authoredWishSpeed = std::max(0.0f, tuning.MaxSpeed) * inputStrength;
        const float acceleratedWishSpeed = tuning.WishSpeedCap > 0.0f
            ? std::min(authoredWishSpeed, tuning.WishSpeedCap)
            : authoredWishSpeed;
        movement::Accelerate(
            planarVelocity,
            wishDirection,
            acceleratedWishSpeed,
            std::max(0.0f, tuning.Acceleration),
            dt);
    }

    if (tuning.Drag > 0.0f)
    {
        const float decay = std::exp(-tuning.Drag * dt);
        planarVelocity *= decay;
        upVelocity *= decay;
    }

    upVelocity += gravity.Dot(output.UpAxis) * output.GravityScale * dt;
    if (support.Kind == SupportKind::Stable && upVelocity < 0.0f)
        upVelocity = 0.0f;

    output.Velocity = planarVelocity + output.UpAxis * upVelocity;
    return output;
}

void FreeLocomotionSystem::Step(World& world, float dt)
{
    StepImpl(world, nullptr, dt);
}

void FreeLocomotionSystem::StepImpl(World& world,
                                    const StoragePartitionSet* partitions,
                                    float dt)
{
    if (!world.IsRegistered<CharacterMovement>()
        || !world.IsRegistered<KinematicState>()
        || !world.IsRegistered<SupportState>()
        || !world.IsRegistered<MovementIntent>()
        || !world.IsRegistered<ResolvedMovementTuning>()
        || !world.IsRegistered<LocomotionOutput>())
    {
        return;
    }

    const LocomotionModeRegistry* modes =
        std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (modes == nullptr || !modes->FreeMode().IsValid())
        return;

    const LocomotionModeId freeMode = modes->FreeMode();

    Query<Write<LocomotionOutput>,
          Read<CharacterMovement>,
          Read<KinematicState>,
          Read<SupportState>,
          Read<MovementIntent>,
          Read<ResolvedMovementTuning>> query(world);

    const auto visit = [&](auto& view)
    {
        auto outputs = view.template Write<LocomotionOutput>();
        const auto movements = view.template Read<CharacterMovement>();
        const auto kinematics = view.template Read<KinematicState>();
        const auto supports = view.template Read<SupportState>();
        const auto intents = view.template Read<MovementIntent>();
        const auto tunings = view.template Read<ResolvedMovementTuning>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            // Another mode owns this character's output this tick.
            if (movements[i].Mode != freeMode)
                continue;

            outputs[i] = StepFreeLocomotion(
                kinematics[i], supports[i], intents[i], tunings[i], Gravity, UpAxis, dt);
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
}

void FreeLocomotionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions, static_cast<float>(ctx.Time.DeltaSeconds));
}
