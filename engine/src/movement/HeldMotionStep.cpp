#include <movement/HeldMotionStep.h>

#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/LocomotionMode.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

namespace
{
void Force(MotionAxisOverride& overrides, Vec3d velocity, Vec3d up, bool vertical)
{
    const float along = velocity.Dot(up);
    overrides.PlanarVelocity = velocity - up * along;
    overrides.HasPlanar = true;
    overrides.ForcedPlanar = true;
    if (!vertical)
        return;
    overrides.UpVelocity = along;
    overrides.HasUp = true;
    overrides.ForcedUp = true;
}

void StepBursts(World& world, MotionBursts& bursts, MotionAxisOverride& overrides, float dt, Vec3d up)
{
    Vec3d sum = Vec3d::Zero();
    bool any = false;
    bool rises = false;
    for (MotionBurst& burst : bursts.Slots)
    {
        if (!burst.Owner.IsSet())
            continue;
        if (!world.IsAlive(burst.Owner.Entity))
        {
            burst = {};
            continue;
        }
        sum = sum + burst.Velocity;
        any = true;
        rises = rises || burst.Rises;
        if (burst.Expires)
        {
            burst.RemainingSeconds -= dt;
            // A whole number of steps lands within rounding of zero, and has ended there.
            if (burst.RemainingSeconds <= dt * 1e-3f)
                burst = {};
        }
    }
    if (any)
        Force(overrides, sum, up, rises);
}

void StepTether(World& world, EntityId entity, TetherSession& tether, MotionAxisOverride& overrides, float dt, Vec3d up)
{
    if (!tether.Owner.IsSet())
        return;
    if (!world.IsAlive(tether.Owner.Entity) || (tether.Anchor.IsValid() && !world.IsAlive(tether.Anchor)))
    {
        tether = {};
        return;
    }
    const World& reader = world;
    const LocalTransform* self = reader.TryGet<LocalTransform>(entity);
    if (self == nullptr)
        return;
    Vec3d anchor = tether.Point;
    if (tether.Anchor.IsValid())
    {
        const LocalTransform* followed = reader.TryGet<LocalTransform>(tether.Anchor);
        if (followed == nullptr)
            return;
        anchor = followed->Value.Position;
    }
    const Vec3d offset = anchor - self->Value.Position;
    const float distance = offset.Magnitude();
    if (distance <= tether.Length || tether.Speed <= 0.0f || dt <= 0.0f)
        return;
    // Never past taut in one step, so the character arrives at the rope's length.
    const float speed = std::min(tether.Speed, (distance - tether.Length) / dt);
    Force(overrides, offset * (speed / distance), up, true);
}
} // namespace

bool HoldsMotion(const MotionBursts& bursts, const TetherSession& tether)
{
    return tether.Owner.IsSet()
        || std::ranges::any_of(bursts.Slots, [](const MotionBurst& burst) { return burst.Owner.IsSet(); });
}

void StepHeldMotion(World& world, EntityId entity, MotionAxisOverride& overrides, float dt, Vec3d upAxis)
{
    const Vec3d up = upAxis.SqrMagnitude() > 0.0f ? upAxis.Normalized() : Vec3d(0.0f, 1.0f, 0.0f);
    if (MotionBursts* bursts = world.TryGet<MotionBursts>(entity))
        StepBursts(world, *bursts, overrides, dt, up);
    if (TetherSession* tether = world.TryGet<TetherSession>(entity))
        StepTether(world, entity, *tether, overrides, dt, up);
}

void HeldMotionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions, static_cast<float>(ctx.Time.DeltaSeconds));
}

void HeldMotionSystem::Step(World& world, float dt)
{
    StepImpl(world, nullptr, dt);
}

void HeldMotionSystem::StepImpl(World& world, const StoragePartitionSet* partitions, float dt)
{
    if (LastWorld != &world)
    {
        Characters.reset();
        LastWorld = &world;
    }
    if (!Characters)
        Characters.emplace(world);
    // Read first, so the many characters holding nothing mark no column changed.
    Held.clear();
    const auto visit = [&](auto& view) {
        const auto bursts = view.template Read<MotionBursts>();
        const auto tethers = view.template Read<TetherSession>();
        const auto modes = view.template Read<LocomotionModeLease>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            if (HoldsMotion(bursts[row], tethers[row]) || modes[row].Owner.IsSet())
                Held.push_back(view.Entity(row));
    };
    if (partitions != nullptr)
        Characters->ForEachChunkIn(*partitions, visit);
    else
        Characters->ForEachChunk(visit);
    const LocomotionModeRegistry* modes = world.TryGetResource<LocomotionModeRegistry>();
    for (const EntityId character : Held)
    {
        if (LocomotionModeLease* lease = world.TryGet<LocomotionModeLease>(character);
            lease != nullptr && lease->Owner.IsSet() && !world.IsAlive(lease->Owner.Entity))
        {
            if (modes != nullptr)
                (void)RequestLocomotionMode(world, character, modes->FreeMode(), ModeRequestClass::Explicit);
            *lease = {};
        }
        MotionAxisOverride* overrides = world.TryGet<MotionAxisOverride>(character);
        const LocomotionOutput* locomotion = static_cast<const World&>(world).TryGet<LocomotionOutput>(character);
        if (overrides != nullptr)
            StepHeldMotion(world, character, *overrides, dt,
                               locomotion != nullptr ? locomotion->UpAxis : Vec3d(0.0f, 1.0f, 0.0f));
    }
}
