#include <physics/SensorOverlapSystem.h>

#include <app/GameContexts.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <world/identity/PersistentEntityIndex.h>

void SensorOverlapSystem::Physics(PhysicsContext& context)
{
    World& world = context.Entities;
    auto* overlaps = world.TryGetResource<SensorOverlaps>();
    if (overlaps == nullptr)
        overlaps = &world.AddResource<SensorOverlaps>();
    if (const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
        identities != nullptr && !overlaps->Awaited().empty())
        overlaps->ForgetAwaitedIf([&](const SensorSeed& seed) {
            const EntityId sensor = identities->TryResolve(seed.Sensor);
            const EntityId occupant = identities->TryResolve(seed.Occupant);
            if (!world.IsAlive(sensor) || !world.IsAlive(occupant))
                return false;
            overlaps->Seed(SensorPair{ sensor, occupant });
            return true;
        });
    overlaps->Begin();
    PhysicsScene.GetSimulation().ObserveSensors(*overlaps);
    PhysicsScene.GetCharacterMovers().ObserveSensors(*overlaps);
    const World& reader = world;
    const auto sampled = [&](SensorPair pair) {
        if (!world.IsAlive(pair.Sensor) || !world.IsAlive(pair.Activator)
            || !context.Partitions.Contains(world.GetEntityPartition(pair.Sensor))
            || !context.Partitions.Contains(world.GetEntityPartition(pair.Activator)))
            return false;
        const auto* collider = world.IsRegistered<Collider>() ? reader.TryGet<Collider>(pair.Sensor) : nullptr;
        const bool sensed = collider != nullptr && collider->IsTrigger && world.IsRegistered<PhysicsBodyLink>()
            && world.HasComponent<PhysicsBodyLink>(pair.Sensor);
        const bool body = world.IsRegistered<PhysicsBodyLink>() && world.HasComponent<PhysicsBodyLink>(pair.Activator);
        const bool character = world.IsRegistered<CharacterMoverLink>() && world.HasComponent<CharacterMoverLink>(pair.Activator);
        return sensed && (body || character);
    };
    overlaps->ForgetIf([&](SensorPair pair) { return !sampled(pair); });
    if (overlaps->Commit(context.Time.TickIndex))
        overlaps->ForgetSeedsIf([&](SensorPair pair) {
            return sampled(pair) || !world.IsAlive(pair.Sensor) || !world.IsAlive(pair.Activator);
        });
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
        const auto departing = [&](SensorPair pair) {
            return !context.Entities.IsAlive(pair.Sensor) || !context.Entities.IsAlive(pair.Activator)
                || context.Entities.GetEntityPartition(pair.Sensor) == change.Partition
                || context.Entities.GetEntityPartition(pair.Activator) == change.Partition;
        };
        if (change.Kind == ZoneResidencyChangeKind::Detaching)
            overlaps->ForgetOccurrencesIf(departing);
        else
            overlaps->ForgetIf(departing);
    }
}
