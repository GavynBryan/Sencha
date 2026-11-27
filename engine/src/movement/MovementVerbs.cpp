#include <movement/MovementVerbs.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/HeldMotion.h>
#include <movement/components/MotionChannels.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
template <typename T, typename F>
void ForEachHeld(World& world, F&& release)
{
    if (!world.IsRegistered<T>())
        return;
    Query<Write<T>> held(world);
    held.ForEachChunk([&](auto& view) {
        auto slots = view.template Write<T>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            release(view.Entity(i), slots[i]);
    });
}

bool HeldBy(const MotionBursts& bursts, const LeaseOwner& owner)
{
    return std::ranges::any_of(bursts.Slots, [&](const MotionBurst& burst) { return burst.Owner == owner; });
}

bool HeldBy(const TetherSession& tether, const LeaseOwner& owner)
{
    return tether.Owner == owner;
}
} // namespace

template <typename T>
bool MovementOperations::Holds(std::optional<Query<Read<T>>>& everyone, const LeaseOwner& owner) const
{
    if (!Entities->IsRegistered<T>())
        return false;
    const World& reader = *Entities;
    if (const T* own = Entities->IsAlive(owner.Entity) ? reader.TryGet<T>(owner.Entity) : nullptr;
        own != nullptr && HeldBy(*own, owner))
        return true;
    if (!everyone)
        everyone.emplace(*Entities);
    bool held = false;
    everyone->ForEachChunk([&](auto& view) {
        const auto slots = view.template Read<T>();
        for (std::uint32_t i = 0; i < view.Count() && !held; ++i)
            held = HeldBy(slots[i], owner);
    });
    return held;
}

VerbAdmission MovementOperations::Burst(const VerbInvocation& invocation, EntityId target, Vec3d direction,
                                        float speed, float seconds, bool rises)
{
    MotionBursts* bursts = Entities->IsRegistered<MotionBursts>() ? Entities->TryGet<MotionBursts>(target) : nullptr;
    if (bursts == nullptr)
        return VerbAdmission::Refused;
    const auto free = std::ranges::find_if(bursts->Slots, [](const MotionBurst& burst) { return !burst.Owner.IsSet(); });
    if (free == std::end(bursts->Slots))
        return VerbAdmission::Refused;
    *free = MotionBurst{ .Owner = invocation.Owner,
                         .Velocity = direction.SqrMagnitude() > 0.0f ? direction.Normalized() * speed : Vec3d::Zero(),
                         .RemainingSeconds = seconds,
                         .Expires = seconds > 0.0f,
                         .Rises = rises };
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::EndBurst(const VerbInvocation& invocation)
{
    ForEachHeld<MotionBursts>(*Entities, [&](EntityId, MotionBursts& bursts) {
        for (MotionBurst& burst : bursts.Slots)
            if (burst.Owner == invocation.Owner)
                burst = {};
    });
    return VerbAdmission::Accepted;
}

bool MovementOperations::BurstHolds(const LeaseOwner& owner) const
{
    return Holds(Bursts, owner);
}

VerbAdmission MovementOperations::Tether(const VerbInvocation& invocation, EntityId target,
                                         std::optional<EntityId> anchor, std::optional<Vec3d> point, float length,
                                         float speed, std::optional<Vec3d> until)
{
    const bool followed = anchor.has_value();
    if (followed ? !Entities->IsAlive(*anchor) : !point.has_value())
        return VerbAdmission::InvalidArguments;
    const Vec3d fixedPoint = point.value_or(Vec3d::Zero());
    float reach = length;
    if (until.has_value())
    {
        const LocalTransform* followedPose =
            followed ? static_cast<const World&>(*Entities).TryGet<LocalTransform>(*anchor) : nullptr;
        if (followed && followedPose == nullptr)
            return VerbAdmission::InvalidArguments;
        reach = (*until - (followed ? followedPose->Value.Position : fixedPoint)).Magnitude();
    }
    TetherSession* tether = Entities->IsRegistered<TetherSession>() ? Entities->TryGet<TetherSession>(target) : nullptr;
    if (tether == nullptr || (tether->Owner.IsSet() && tether->Owner != invocation.Owner && !tether->Yields))
        return VerbAdmission::Refused;
    *tether = TetherSession{ .Owner = invocation.Owner,
                             .Anchor = followed ? *anchor : EntityId{},
                             .Point = fixedPoint,
                             .Length = reach,
                             .Speed = speed,
                             .Arrives = until.has_value() };
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::EndTether(const VerbInvocation& invocation)
{
    ForEachHeld<TetherSession>(*Entities, [&](EntityId, TetherSession& tether) {
        if (tether.Owner == invocation.Owner)
            tether = {};
    });
    return VerbAdmission::Accepted;
}

bool MovementOperations::TetherHolds(const LeaseOwner& owner) const
{
    return Holds(Tethers, owner);
}

VerbAdmission MovementOperations::Mode(const VerbInvocation& invocation, EntityId target, std::string_view name)
{
    const LocomotionModeRegistry* modes = Entities->TryGetResource<LocomotionModeRegistry>();
    const LocomotionModeEntry* mode = modes != nullptr ? modes->Find(name) : nullptr;
    LocomotionModeLease* lease =
        Entities->IsRegistered<LocomotionModeLease>() ? Entities->TryGet<LocomotionModeLease>(target) : nullptr;
    if (mode == nullptr || lease == nullptr || (lease->Owner.IsSet() && lease->Owner != invocation.Owner)
        || !RequestLocomotionMode(*Entities, target, mode->Id, ModeRequestClass::Explicit))
        return VerbAdmission::Refused;
    *lease = LocomotionModeLease{ .Owner = invocation.Owner, .Mode = mode->Id };
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::EndMode(const VerbInvocation& invocation)
{
    const LocomotionModeRegistry* modes = Entities->TryGetResource<LocomotionModeRegistry>();
    ForEachHeld<LocomotionModeLease>(*Entities, [&](EntityId character, LocomotionModeLease& lease) {
        if (lease.Owner != invocation.Owner)
            return;
        const CharacterMovement* movement = static_cast<const World&>(*Entities).TryGet<CharacterMovement>(character);
        if (modes != nullptr && movement != nullptr && movement->Mode == lease.Mode)
            (void)RequestLocomotionMode(*Entities, character, modes->FreeMode(), ModeRequestClass::Explicit);
        lease = {};
    });
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::Velocity(EntityId target, Vec3d direction, float speed, float rise, Vec3d& velocity,
                                           Vec3d& up) const
{
    const float length = direction.Magnitude();
    if (!std::isfinite(length) || (length <= 1.0e-6f && speed != 0.0f))
        return VerbAdmission::InvalidArguments;
    const World& reader = *Entities;
    if (reader.TryGet<CharacterMovement>(target) == nullptr)
        return VerbAdmission::Refused;
    const LocomotionOutput* locomotion =
        reader.IsRegistered<LocomotionOutput>() ? reader.TryGet<LocomotionOutput>(target) : nullptr;
    up = locomotion != nullptr ? locomotion->UpAxis.Normalized() : Vec3d(0.0f, 1.0f, 0.0f);
    velocity = (length > 1.0e-6f ? direction * (speed / length) : Vec3d::Zero()) + up * rise;
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::Launch(EntityId target, Vec3d direction, float speed, float rise)
{
    Vec3d velocity;
    Vec3d up;
    if (const VerbAdmission read = Velocity(target, direction, speed, rise, velocity, up); read != VerbAdmission::Accepted)
        return read;
    const float upward = velocity.Dot(up);
    const bool planar = ForceSetPlanarMotionOverride(*Entities, target, velocity - up * upward);
    const bool vertical = ForceSetUpMotionOverride(*Entities, target, upward);
    return planar && vertical ? VerbAdmission::Accepted : VerbAdmission::Refused;
}

VerbAdmission MovementOperations::Impulse(EntityId target, Vec3d direction, float speed, float rise)
{
    Vec3d velocity;
    Vec3d up;
    if (const VerbAdmission read = Velocity(target, direction, speed, rise, velocity, up); read != VerbAdmission::Accepted)
        return read;
    return AddMotionImpulse(*Entities, target, velocity) ? VerbAdmission::Accepted : VerbAdmission::Refused;
}
