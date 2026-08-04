#include "MovementResponseSim.h"

#include <movement/FreeLocomotionSystem.h>
#include <movement/MovementIntent.h>

#include <algorithm>
#include <cmath>

namespace
{
    // The tag the jump ability grants for one tick. Tuning resolution reads it
    // before jump execution consumes it, which is what lets a profile change
    // coefficients on the launch tick itself.
    const std::vector<std::string> kJumpTags = { "movement.jump.requested" };
    const std::vector<std::string> kNoTags = {};

    Vec3d ProjectOntoPlane(const Vec3d& value, const Vec3d& unitNormal)
    {
        return value - unitNormal * value.Dot(unitNormal);
    }

    SupportState MakeSupport(SupportKind kind, Vec3d upAxis)
    {
        SupportState support;
        support.Kind = kind;
        support.Normal = upAxis;
        return support;
    }

    MovementSimSample MakeSample(float time, const Vec3d& position, const Vec3d& velocity,
                                 const Vec3d& upAxis, const Vec3d& launch)
    {
        const Vec3d offset = position - launch;
        return MovementSimSample{
            .Time = time,
            .Height = offset.Dot(upAxis),
            .Distance = ProjectOntoPlane(offset, upAxis).Magnitude(),
            .PlanarSpeed = ProjectOntoPlane(velocity, upAxis).Magnitude(),
        };
    }
}

MovementJumpArc SimulateJumpArc(const BoundMovementProfile& profile,
                                const MovementSimContextFn& makeContext,
                                const MovementSimParams& params)
{
    MovementJumpArc arc;
    if (!(params.Dt > 0.0f) || params.MaxTicks <= 0)
        return arc;

    const Vec3d upAxis = params.UpAxis;
    MovementIntent intent;
    // Full forward input, perpendicular to the up axis, so the hop covers ground.
    const Vec3d forward = std::abs(upAxis.Dot(Vec3d(0.0f, 0.0f, 1.0f))) > 0.9f
        ? Vec3d(1.0f, 0.0f, 0.0f) : Vec3d(0.0f, 0.0f, 1.0f);
    intent.WishDir = ProjectOntoPlane(forward, upAxis).Normalized();

    KinematicState kinematic;
    Vec3d position = Vec3d::Zero();
    const Vec3d launch = position;
    float time = 0.0f;

    arc.Samples.push_back(MakeSample(time, position, kinematic.Velocity, upAxis, launch));

    for (int tick = 0; tick < params.MaxTicks; ++tick)
    {
        const bool launching = tick == 0;
        const SupportKind support = launching ? SupportKind::Stable : SupportKind::None;

        GameplayTagContainer tags;
        const MovementResolveContext context = makeContext(
            support, launching ? std::span<const std::string>(kJumpTags)
                               : std::span<const std::string>(kNoTags),
            tags);
        const MovementResolveResult resolved =
            ResolveMovementTuning(profile, context, params.MoveSpeed, false);

        const SupportState supportState = MakeSupport(support, upAxis);
        const LocomotionOutput output = StepFreeLocomotion(
            kinematic, supportState, intent, resolved.Tuning,
            params.Gravity, upAxis, params.Dt);

        Vec3d velocity = output.Velocity;
        if (launching)
        {
            // Jump execution replaces the up channel outright; composition then
            // rebuilds the velocity from the planar part plus that override.
            arc.JumpSpeed = resolved.Tuning.JumpSpeed;
            velocity = ProjectOntoPlane(velocity, upAxis) + upAxis * resolved.Tuning.JumpSpeed;
        }

        kinematic.Velocity = velocity;
        position += velocity * params.Dt;
        time += params.Dt;

        const MovementSimSample sample = MakeSample(time, position, velocity, upAxis, launch);
        arc.Samples.push_back(sample);

        if (sample.Height > arc.ApexHeight)
        {
            arc.ApexHeight = sample.Height;
            arc.TimeToApex = sample.Time;
        }

        // Back to launch height on the way down: the hop is over.
        if (!launching && sample.Height <= 0.0f && velocity.Dot(upAxis) < 0.0f)
        {
            arc.Landed = true;
            arc.HangTime = sample.Time;
            arc.Distance = sample.Distance;
            break;
        }
    }

    if (!arc.Landed && !arc.Samples.empty())
    {
        arc.HangTime = arc.Samples.back().Time;
        arc.Distance = arc.Samples.back().Distance;
    }
    return arc;
}

MovementSpeedResponse SimulateSpeedResponse(const BoundMovementProfile& profile,
                                            const MovementSimContextFn& makeContext,
                                            const MovementSimParams& params)
{
    MovementSpeedResponse response;
    if (!(params.Dt > 0.0f) || params.MaxTicks <= 0)
        return response;

    const Vec3d upAxis = params.UpAxis;
    MovementIntent intent;
    const Vec3d forward = std::abs(upAxis.Dot(Vec3d(0.0f, 0.0f, 1.0f))) > 0.9f
        ? Vec3d(1.0f, 0.0f, 0.0f) : Vec3d(0.0f, 0.0f, 1.0f);
    intent.WishDir = ProjectOntoPlane(forward, upAxis).Normalized();

    KinematicState kinematic;
    Vec3d position = Vec3d::Zero();
    const Vec3d launch = position;
    float time = 0.0f;

    const SupportState supportState = MakeSupport(SupportKind::Stable, upAxis);
    response.Samples.push_back(MakeSample(time, position, kinematic.Velocity, upAxis, launch));

    // Accelerate from rest until the speed stops climbing meaningfully.
    const int accelerateTicks = params.MaxTicks / 2;
    for (int tick = 0; tick < accelerateTicks; ++tick)
    {
        GameplayTagContainer tags;
        const MovementResolveContext context =
            makeContext(SupportKind::Stable, std::span<const std::string>(kNoTags), tags);
        const MovementResolveResult resolved =
            ResolveMovementTuning(profile, context, params.MoveSpeed, false);

        const LocomotionOutput output = StepFreeLocomotion(
            kinematic, supportState, intent, resolved.Tuning,
            params.Gravity, upAxis, params.Dt);

        kinematic.Velocity = output.Velocity;
        position += kinematic.Velocity * params.Dt;
        time += params.Dt;
        response.Samples.push_back(
            MakeSample(time, position, kinematic.Velocity, upAxis, launch));
    }

    response.TopSpeed = response.Samples.empty() ? 0.0f : response.Samples.back().PlanarSpeed;

    if (response.TopSpeed > 0.0f)
    {
        const float threshold = response.TopSpeed * 0.95f;
        for (const MovementSimSample& sample : response.Samples)
        {
            if (sample.PlanarSpeed >= threshold)
            {
                response.TimeToTopSpeed = sample.Time;
                response.ReachedTopSpeed = true;
                break;
            }
        }
    }

    // Release the input and let friction settle the character.
    const Vec3d stopStart = position;
    intent.WishDir = Vec3d::Zero();
    for (int tick = 0; tick < params.MaxTicks; ++tick)
    {
        GameplayTagContainer tags;
        const MovementResolveContext context =
            makeContext(SupportKind::Stable, std::span<const std::string>(kNoTags), tags);
        const MovementResolveResult resolved =
            ResolveMovementTuning(profile, context, params.MoveSpeed, false);

        const LocomotionOutput output = StepFreeLocomotion(
            kinematic, supportState, intent, resolved.Tuning,
            params.Gravity, upAxis, params.Dt);

        kinematic.Velocity = output.Velocity;
        position += kinematic.Velocity * params.Dt;
        time += params.Dt;
        response.Samples.push_back(
            MakeSample(time, position, kinematic.Velocity, upAxis, launch));

        if (ProjectOntoPlane(kinematic.Velocity, upAxis).Magnitude() < 0.05f)
            break;
    }
    response.StoppingDistance = ProjectOntoPlane(position - stopStart, upAxis).Magnitude();
    return response;
}
