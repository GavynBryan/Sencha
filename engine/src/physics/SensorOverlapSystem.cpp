#include <physics/SensorOverlapSystem.h>

#include <app/GameContexts.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>

void SensorOverlapSystem::Physics(PhysicsContext& context)
{
    World& world = context.Entities;
    auto* overlaps = world.TryGetResource<SensorOverlaps>();
    if (overlaps == nullptr)
        overlaps = &world.AddResource<SensorOverlaps>();
    overlaps->Begin();
    PhysicsScene.GetSimulation().ObserveSensors(*overlaps);
    PhysicsScene.GetCharacterMovers().ObserveSensors(*overlaps);
    const World& reader = world;
    overlaps->ForgetIf([&](SensorPair pair) {
        if (!world.IsAlive(pair.Sensor) || !world.IsAlive(pair.Activator)
            || !context.Partitions.Contains(world.GetEntityPartition(pair.Sensor))
            || !context.Partitions.Contains(world.GetEntityPartition(pair.Activator)))
            return true;
        const auto* collider = world.IsRegistered<Collider>() ? reader.TryGet<Collider>(pair.Sensor) : nullptr;
        const bool body = world.IsRegistered<PhysicsBodyLink>() && world.HasComponent<PhysicsBodyLink>(pair.Activator);
        const bool character = world.IsRegistered<CharacterMoverLink>() && world.HasComponent<CharacterMoverLink>(pair.Activator);
        return collider == nullptr || !collider->IsTrigger || (!body && !character);
    });
    (void)overlaps->Commit(context.Time.TickIndex);
}

void SensorOverlapSystem::ZoneResidency(ZoneResidencyContext& context)
{
    auto* overlaps = context.Entities.TryGetResource<SensorOverlaps>();
    if (overlaps == nullptr)
        return;
    for (const auto& change : context.Changes)
    {
        if (change.Kind != ZoneResidencyChangeKind::Detaching
            && !(change.Previous.Physics && !change.Current.Physics))
            continue;
        overlaps->ForgetIf([&](SensorPair pair) {
            return !context.Entities.IsAlive(pair.Sensor) || !context.Entities.IsAlive(pair.Activator)
                || context.Entities.GetEntityPartition(pair.Sensor) == change.Partition
                || context.Entities.GetEntityPartition(pair.Activator) == change.Partition;
        });
    }
}
