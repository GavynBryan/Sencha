#include <movement/MovementVerbs.h>

#include <authored/AuthoredValue.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/HeldMotion.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace
{
VerbDefinition MovementVerb(std::string_view name, std::string displayName, std::string description)
{
    VerbDefinition lease;
    lease.Name = std::string(name);
    lease.DisplayName = std::move(displayName);
    lease.Description = std::move(description);
    lease.Category = "Movement";
    lease.Kind = VerbKind::Lease;
    lease.Prediction = AuthoredPrediction::Simulation;
    DataFieldSchema target = MakeDataField(DataFieldKind::Entity, "target", "Target", "The character.");
    target.Reference.ComponentIdentity = std::string(ComponentDefinition<CharacterMovement>::Identity);
    lease.Arguments.Children.push_back(std::move(target));
    return lease;
}

DataFieldSchema Number(std::string key, std::string label, std::string summary, double fallback, double minimum)
{
    DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(label), std::move(summary));
    field.Required = false;
    field.Default = fallback;
    field.Numeric.Minimum = minimum;
    return field;
}

Vec3d VectorOf(const AuthoredVectorValue& value)
{
    return Vec3d(static_cast<float>(value.Components[0]), static_cast<float>(value.Components[1]),
                 static_cast<float>(value.Components[2]));
}

// Every slot of `T` the owner holds, on whichever character it is.
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
} // namespace

void DeclareMovementVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition burst = MovementVerb(kMovementBurstLease, "Burst of motion",
                                 "Drives the character at a velocity while held, or for a time.");
    DataFieldSchema direction =
        MakeDataField(DataFieldKind::Vector, "direction", "Direction", "Which way, as aim.direction gives it.");
    direction.VectorLength = 3;
    burst.Arguments.Children.push_back(std::move(direction));
    burst.Arguments.Children.push_back(Number("speed", "Speed", "Metres per second of the character's time.", 10.0, 0.0));
    burst.Arguments.Children.push_back(Number("seconds", "Seconds", "How long; zero holds it until let go.", 0.0, 0.0));
    DataFieldSchema rises = MakeDataField(DataFieldKind::Bool, "rises", "Rises", "Its upward part lifts the character too.");
    rises.Required = false;
    rises.Default = false;
    burst.Arguments.Children.push_back(std::move(rises));
    burst.Completes = true;
    (void)scope.Declare(std::move(burst));

    VerbDefinition tether = MovementVerb(kMovementTetherLease, "Tether",
                                  "Pulls the character toward an anchor once it is farther away than the rope.");
    DataFieldSchema anchor = MakeDataField(DataFieldKind::Entity, "anchor", "Anchor", "An entity to follow; it ends the tether when it goes.");
    anchor.Required = false;
    tether.Arguments.Children.push_back(std::move(anchor));
    DataFieldSchema point = MakeDataField(DataFieldKind::Vector, "point", "Point", "Where the anchor is, when no entity is.");
    point.VectorLength = 3;
    point.Required = false;
    tether.Arguments.Children.push_back(std::move(point));
    tether.Arguments.Children.push_back(Number("length", "Length", "Metres of rope.", 2.0, 0.0));
    tether.Arguments.Children.push_back(Number("speed", "Speed", "How fast it pulls, in metres per second.", 12.0, 0.0));
    tether.Completes = true;
    (void)scope.Declare(std::move(tether));

    VerbDefinition mode = MovementVerb(kMovementModeLease, "Locomotion mode",
                                "Puts the character in a locomotion mode while held.");
    mode.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::String, "mode", "Mode", "The mode's name, as movement.mode.<name>."));
    (void)scope.Declare(std::move(mode));

    VerbDefinition velocity = MovementVerb(kMovementApplyVelocityVerb, "Apply velocity",
                                    "Changes the character's world velocity once; subsequent ticks retain the achieved motion.");
    velocity.Kind = VerbKind::Instant;
    velocity.Prediction = AuthoredPrediction::AuthorityOnly;
    DataFieldSchema way = MakeDataField(DataFieldKind::Vector, "direction", "Direction", "Normalized safely before applying speed.");
    way.VectorLength = 3;
    way.Editor.Widget = "direction";
    velocity.Arguments.Children.push_back(std::move(way));
    DataFieldSchema speed = Number("speed", "Speed", "Metres per second.", 10.0, 0.0);
    speed.Numeric.Maximum = std::numeric_limits<float>::max();
    velocity.Arguments.Children.push_back(std::move(speed));
    DataFieldSchema operation = MakeDataField(DataFieldKind::Enum, "operation", "Operation");
    operation.EnumChoices = { { "add", "Add", "Adds velocity to the composed motion." },
                              { "replace", "Replace", "Replaces all world velocity, including support motion and earlier impulses." } };
    operation.Required = false;
    operation.Default = std::string("replace");
    velocity.Arguments.Children.push_back(std::move(operation));
    (void)scope.Declare(std::move(velocity));
}

VerbAdmission MovementOperations::ApplyVelocity(const VerbInvocation& invocation)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    EntityId target;
    AuthoredVectorValue direction;
    double speed = 0.0;
    std::string_view operation;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetVector(1, direction) || direction.Length != 3
        || !invocation.Arguments->TryGetFloat(2, speed)
        || !invocation.Arguments->TryGetEnum(3, operation)
        || !std::isfinite(speed) || speed < 0.0 || speed > std::numeric_limits<float>::max()
        || (operation != "add" && operation != "replace") || !Entities->IsAlive(target))
        return VerbAdmission::InvalidArguments;
    const auto& d = direction.Components;
    const double length = std::hypot(d[0], d[1], d[2]);
    if (!std::isfinite(length) || length <= 1.0e-8)
        return VerbAdmission::InvalidArguments;
    if (!Entities->IsRegistered<CharacterMovement>() || !Entities->HasComponent<CharacterMovement>(target))
        return VerbAdmission::Refused;
    const Vec3d velocity(static_cast<float>(d[0] / length * speed),
                         static_cast<float>(d[1] / length * speed),
                         static_cast<float>(d[2] / length * speed));
    const bool applied = operation == "add" ? AddMotionImpulse(*Entities, target, velocity)
                                             : ReplaceMotionVelocity(*Entities, target, velocity);
    return applied ? VerbAdmission::Accepted : VerbAdmission::Refused;
}

namespace
{
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
bool MovementOperations::Holds(std::optional<Query<Read<T>>>& everyone, const LeaseOwner& owner)
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

VerbAdmission MovementOperations::Burst(const VerbInvocation& invocation)
{
    if (invocation.Step == LeaseStep::Probe)
        return Holds(Bursts, invocation.Owner) ? VerbAdmission::Accepted : VerbAdmission::Refused;
    if (invocation.Step == LeaseStep::Release)
    {
        ForEachHeld<MotionBursts>(*Entities, [&](EntityId, MotionBursts& bursts) {
            for (MotionBurst& burst : bursts.Slots)
                if (burst.Owner == invocation.Owner)
                    burst = {};
        });
        return VerbAdmission::Accepted;
    }
    EntityId target;
    AuthoredVectorValue direction;
    double speed = 0.0;
    double seconds = 0.0;
    bool rises = false;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetVector(1, direction) || !invocation.Arguments->TryGetFloat(2, speed)
        || !invocation.Arguments->TryGetFloat(3, seconds) || !invocation.Arguments->TryGetBool(4, rises)
        || !Entities->IsAlive(target))
        return VerbAdmission::InvalidArguments;
    MotionBursts* bursts = Entities->IsRegistered<MotionBursts>() ? Entities->TryGet<MotionBursts>(target) : nullptr;
    if (bursts == nullptr)
        return VerbAdmission::Refused;
    const auto free = std::ranges::find_if(bursts->Slots, [](const MotionBurst& burst) { return !burst.Owner.IsSet(); });
    if (free == std::end(bursts->Slots))
        return VerbAdmission::Refused;
    const Vec3d way = VectorOf(direction);
    *free = MotionBurst{ .Owner = invocation.Owner,
                         .Velocity = way.SqrMagnitude() > 0.0f ? way.Normalized() * static_cast<float>(speed) : Vec3d::Zero(),
                         .RemainingSeconds = static_cast<float>(seconds),
                         .Expires = seconds > 0.0,
                         .Rises = rises };
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::Tether(const VerbInvocation& invocation)
{
    if (invocation.Step == LeaseStep::Probe)
        return Holds(Tethers, invocation.Owner) ? VerbAdmission::Accepted : VerbAdmission::Refused;
    if (invocation.Step == LeaseStep::Release)
    {
        ForEachHeld<TetherSession>(*Entities, [&](EntityId, TetherSession& tether) {
            if (tether.Owner == invocation.Owner)
                tether = {};
        });
        return VerbAdmission::Accepted;
    }
    EntityId target;
    double length = 0.0;
    double speed = 0.0;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetFloat(3, length) || !invocation.Arguments->TryGetFloat(4, speed)
        || !Entities->IsAlive(target))
        return VerbAdmission::InvalidArguments;
    EntityId anchor;
    AuthoredVectorValue point;
    const bool followed = invocation.Arguments->TryGetEntity(1, anchor);
    if (followed ? !Entities->IsAlive(anchor) : !invocation.Arguments->TryGetVector(2, point))
        return VerbAdmission::InvalidArguments;
    TetherSession* tether = Entities->IsRegistered<TetherSession>() ? Entities->TryGet<TetherSession>(target) : nullptr;
    if (tether == nullptr || (tether->Owner.IsSet() && tether->Owner != invocation.Owner))
        return VerbAdmission::Refused;
    *tether = TetherSession{ .Owner = invocation.Owner,
                             .Anchor = followed ? anchor : EntityId{},
                             .Point = followed ? Vec3d::Zero() : VectorOf(point),
                             .Length = static_cast<float>(length),
                             .Speed = static_cast<float>(speed) };
    return VerbAdmission::Accepted;
}

VerbAdmission MovementOperations::Mode(const VerbInvocation& invocation)
{
    const LocomotionModeRegistry* modes = Entities->TryGetResource<LocomotionModeRegistry>();
    if (invocation.Step == LeaseStep::Release)
    {
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
    EntityId target;
    std::string_view name;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, target)
        || !invocation.Arguments->TryGetString(1, name) || !Entities->IsAlive(target))
        return VerbAdmission::InvalidArguments;
    const LocomotionModeEntry* mode = modes != nullptr ? modes->Find(name) : nullptr;
    LocomotionModeLease* lease =
        Entities->IsRegistered<LocomotionModeLease>() ? Entities->TryGet<LocomotionModeLease>(target) : nullptr;
    if (mode == nullptr || lease == nullptr || (lease->Owner.IsSet() && lease->Owner != invocation.Owner)
        || !RequestLocomotionMode(*Entities, target, mode->Id, ModeRequestClass::Explicit))
        return VerbAdmission::Refused;
    *lease = LocomotionModeLease{ .Owner = invocation.Owner, .Mode = mode->Id };
    return VerbAdmission::Accepted;
}
