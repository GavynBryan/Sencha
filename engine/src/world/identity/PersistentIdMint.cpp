#include <world/identity/PersistentIdMint.h>

#include <ecs/World.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentIdComponent.h>

PersistentEntityId MintRuntimePersistentId(World& world)
{
    if (!IsSimulationAuthority(world))
        return {};
    PersistentIdMint& mint = world.HasResource<PersistentIdMint>() ? world.GetResource<PersistentIdMint>()
                                                                    : world.AddResource<PersistentIdMint>();
    return PersistentEntityId{ PersistentEntityIdRuntimeBit | mint.Next++ };
}

PersistentEntityId EnsurePersistentId(World& world, EntityId entity)
{
    if (!world.IsAlive(entity) || !world.IsRegistered<PersistentIdComponent>())
        return {};
    if (const PersistentIdComponent* identity = world.TryGet<PersistentIdComponent>(entity))
        return identity->Id;
    const PersistentEntityId minted = MintRuntimePersistentId(world);
    if (minted.IsValid())
        world.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ minted });
    return minted;
}

bool HasAuthoredPersistentId(const World& world, EntityId entity)
{
    const PersistentIdComponent* identity = world.IsRegistered<PersistentIdComponent>() && world.IsAlive(entity)
        ? world.TryGet<PersistentIdComponent>(entity)
        : nullptr;
    return identity != nullptr && IsAuthoredPersistentEntityId(identity->Id);
}
