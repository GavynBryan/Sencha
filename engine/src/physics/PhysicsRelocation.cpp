#include <physics/PhysicsRegistration.h>

#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/SensorOverlaps.h>
#include <world/relocation/RelocationRules.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <utility>

namespace
{
    void TurnVelocities(RigidBody& body, const RelocationFrame& frame)
    {
        body.LinearVelocity = frame.MapVector(body.LinearVelocity);
        body.AngularVelocity = frame.MapVector(body.AngularVelocity);
    }

    void ForgetSensorPairs(void*, World& world, std::span<const EntityId> group, const RelocationFrame&)
    {
        SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>();
        if (overlaps == nullptr)
            return;
        overlaps->ForgetIf([&](const SensorPair& pair) {
            return std::ranges::find(group, pair.Sensor) != group.end()
                || std::ranges::find(group, pair.Activator) != group.end();
        });
    }

    // A live body's pose and velocities are pulled from the backend each step.
    void MirrorBody(void* context, World& world, EntityId entity, const RelocationFrame&)
    {
        PhysicsWorld& simulation = *static_cast<PhysicsWorld*>(context);
        const World& reader = world;
        const PhysicsBodyLink* link = reader.TryGet<PhysicsBodyLink>(entity);
        const WorldTransform* placed = world.IsRegistered<WorldTransform>() ? reader.TryGet<WorldTransform>(entity) : nullptr;
        const LocalTransform* local = reader.TryGet<LocalTransform>(entity);
        if (link == nullptr || !link->Body.IsValid() || (placed == nullptr && local == nullptr))
            return;
        const Transform3f& pose = placed != nullptr ? placed->Value : local->Value;
        simulation.TeleportBody(link->Body, pose.Position, pose.Rotation);
        const RigidBody* body = world.IsRegistered<RigidBody>() ? reader.TryGet<RigidBody>(entity) : nullptr;
        if (body != nullptr && body->Motion == BodyMotion::Dynamic && !body->Asleep)
        {
            simulation.SetLinearVelocity(link->Body, body->LinearVelocity);
            simulation.SetAngularVelocity(link->Body, body->AngularVelocity);
        }
    }

    void MirrorMover(void* context, World& world, EntityId entity, const RelocationFrame&)
    {
        CharacterMoverPool& movers = *static_cast<CharacterMoverPool*>(context);
        const LocalTransform* local = std::as_const(world).TryGet<LocalTransform>(entity);
        if (local != nullptr)
            (void)movers.RestorePosition(world, entity, local->Value.Position);
    }
}

void RegisterPhysicsRelocation(World& world)
{
    SetRelocationRule<RigidBody>(world, RelocationRuleKind::Maps, &RelocateValue<RigidBody, &TurnVelocities>);
    EnsureRelocationRules(world).AddGroupRule(RelocationRules::GroupRule{ .Relocate = &ForgetSensorPairs });
}

void InstallPhysicsBackendRelocation(World& world, PhysicsWorld& simulation, CharacterMoverPool& movers)
{
    SetRelocationRule<PhysicsBodyLink>(world, RelocationRuleKind::Mirrors, &MirrorBody, &simulation);
    SetRelocationRule<CharacterMoverLink>(world, RelocationRuleKind::Mirrors, &MirrorMover, &movers);
}
