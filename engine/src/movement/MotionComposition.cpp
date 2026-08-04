#include <movement/MotionComposition.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <movement/MovementComponents.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>

#include <cmath>

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

bool TrySetPlanarMotionOverride(World& world, EntityId entity, Vec3d velocity)
{
    if (!world.IsRegistered<MotionAxisOverride>())
        return false;
    MotionAxisOverride* overrideState = world.TryGet<MotionAxisOverride>(entity);
    if (overrideState == nullptr || overrideState->HasPlanar)
        return false;
    overrideState->PlanarVelocity = velocity;
    overrideState->HasPlanar = true;
    overrideState->ForcedPlanar = false;
    return true;
}

bool TrySetUpMotionOverride(World& world, EntityId entity, float velocity)
{
    if (!world.IsRegistered<MotionAxisOverride>())
        return false;
    MotionAxisOverride* overrideState = world.TryGet<MotionAxisOverride>(entity);
    if (overrideState == nullptr || overrideState->HasUp)
        return false;
    overrideState->UpVelocity = velocity;
    overrideState->HasUp = true;
    overrideState->ForcedUp = false;
    return true;
}

bool ForceSetPlanarMotionOverride(World& world, EntityId entity, Vec3d velocity)
{
    if (!world.IsRegistered<MotionAxisOverride>())
        return false;
    MotionAxisOverride* overrideState = world.TryGet<MotionAxisOverride>(entity);
    if (overrideState == nullptr)
        return false;
    overrideState->PlanarVelocity = velocity;
    overrideState->HasPlanar = true;
    overrideState->ForcedPlanar = true;
    return true;
}

bool ForceSetUpMotionOverride(World& world, EntityId entity, float velocity)
{
    if (!world.IsRegistered<MotionAxisOverride>())
        return false;
    MotionAxisOverride* overrideState = world.TryGet<MotionAxisOverride>(entity);
    if (overrideState == nullptr)
        return false;
    overrideState->UpVelocity = velocity;
    overrideState->HasUp = true;
    overrideState->ForcedUp = true;
    return true;
}

bool AddMotionImpulse(World& world, EntityId entity, Vec3d deltaVelocity)
{
    if (!world.IsRegistered<MotionImpulse>())
        return false;
    MotionImpulse* impulses = world.TryGet<MotionImpulse>(entity);
    if (impulses == nullptr)
        return false;
    impulses->DeltaVelocity += deltaVelocity;
    return true;
}

MotionRequest ComposeMotion(const LocomotionOutput& locomotion,
                            const SupportState* support,
                            const MotionAxisOverride& overrides,
                            const MotionImpulse& impulses)
{
    MotionRequest request;
    request.UpAxis = SafeUpAxis(locomotion.UpAxis);
    request.GravityScale = std::isfinite(locomotion.GravityScale)
        ? locomotion.GravityScale
        : 1.0f;

    // Locomotion velocity is the controllable, support-relative world-space
    // velocity. Overrides replace its planar/up channels in that same relative
    // frame. Support velocity is added afterward, preserving platform inheritance
    // even when jump or dash replaces one controllable channel.
    Vec3d relativePlanar = ProjectOntoPlane(locomotion.Velocity, request.UpAxis);
    float relativeUp = locomotion.Velocity.Dot(request.UpAxis);

    if (overrides.HasPlanar)
        relativePlanar = ProjectOntoPlane(overrides.PlanarVelocity, request.UpAxis);
    if (overrides.HasUp)
        relativeUp = overrides.UpVelocity;

    const Vec3d supportVelocity = support != nullptr && support->Kind == SupportKind::Stable
        ? support->SurfaceVelocity
        : Vec3d::Zero();

    request.Velocity = relativePlanar
        + request.UpAxis * relativeUp
        + supportVelocity
        + impulses.DeltaVelocity;
    return request;
}

void MotionCompositionSystem::Step(World& world)
{
    StepImpl(world, nullptr);
}

void MotionCompositionSystem::StepImpl(World& world, const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<MotionRequest>()
        || !world.IsRegistered<LocomotionOutput>()
        || !world.IsRegistered<MotionAxisOverride>()
        || !world.IsRegistered<MotionImpulse>())
    {
        return;
    }

    const bool hasSupport = world.IsRegistered<SupportState>();

    Query<Write<MotionRequest>,
          Read<LocomotionOutput>,
          Write<MotionAxisOverride>,
          Write<MotionImpulse>> query(world);

    const auto visit = [&](auto& view)
    {
        auto requests = view.template Write<MotionRequest>();
        const auto locomotion = view.template Read<LocomotionOutput>();
        auto overrides = view.template Write<MotionAxisOverride>();
        auto impulses = view.template Write<MotionImpulse>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const SupportState* support = hasSupport
                ? std::as_const(world).TryGet<SupportState>(view.Entity(i))
                : nullptr;

            requests[i] = ComposeMotion(locomotion[i], support, overrides[i], impulses[i]);

            // The contribution components are one-fixed-tick mailboxes. A
            // producer that runs after composition is a schedule bug; its
            // value deliberately survives to the next tick rather than being
            // silently dropped here.
            overrides[i] = MotionAxisOverride{};
            impulses[i] = MotionImpulse{};
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
}

void MotionCompositionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}
