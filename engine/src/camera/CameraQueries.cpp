#include <camera/CameraQueries.h>

#include <components/CameraComponent.h>

EntityId FirstAuthoredCamera(const World& world, StoragePartitionId partition)
{
    if (!world.IsRegistered<CameraComponent>())
        return EntityId{};

    for (const EntityId entity : world.GetAliveEntities())
    {
        if (world.GetEntityPartition(entity) == partition
            && world.TryGet<CameraComponent>(entity) != nullptr)
        {
            return entity;
        }
    }
    return EntityId{};
}
