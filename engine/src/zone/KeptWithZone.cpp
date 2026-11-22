#include <zone/KeptWithZone.h>

#include <ecs/World.h>
#include <world/SimulationAuthority.h>
#include <world/held/Held.h>
#include <world/identity/PersistentIdMint.h>

bool KeepWithZone(World& world, EntityId entity)
{
    if (!IsSimulationAuthority(world) || !world.IsAlive(entity) || !world.IsRegistered<KeptWithZone>()
        || HasAuthoredPersistentId(world, entity) || (world.IsRegistered<Held>() && world.HasComponent<Held>(entity)))
        return false;
    if (!EnsurePersistentId(world, entity).IsValid())
        return false;
    if (!world.HasComponent<KeptWithZone>(entity))
        world.AddComponent<KeptWithZone>(entity);
    return true;
}

bool IsKeptWithZone(const World& world, EntityId entity)
{
    return world.IsRegistered<KeptWithZone>() && world.IsAlive(entity) && world.HasComponent<KeptWithZone>(entity);
}

ZoneStateForgetting ForgettingOfKept(const World& world, EntityId entity)
{
    const ZoneStateLifetime* lifetime =
        world.IsRegistered<ZoneStateLifetime>() ? world.TryGet<ZoneStateLifetime>(entity) : nullptr;
    return lifetime != nullptr ? lifetime->Forgotten : ZoneStateForgetting::Never;
}
