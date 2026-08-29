#include <physics/ZoneCollisionLoader.h>

#include <core/io/FileBytes.h>

#include <cstddef>
#include <vector>

#include <ecs/ArchetypeSignature.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <physics/CollisionShapeCache.h>
#include <physics/components/Collider.h>
#include <world/transform/TransformComponents.h>

int LoadZoneCollision(
    World& world,
    CollisionShapeCache& cache,
    std::span<const SmapCollisionCell> cells,
    const std::string& cookedRoot,
    StoragePartitionId partition)
{
    if (cells.empty())
        return 0;

    ArchetypeSignature colliderSignature;
    colliderSignature.set(world.GetComponentId<LocalTransform>());
    colliderSignature.set(world.GetComponentId<Collider>());

    int loaded = 0;
    for (const SmapCollisionCell& cell : cells)
    {
        std::vector<std::byte> bytes;
        if (!ReadFileBytes(cookedRoot + "/" + cell.BlobPath, bytes)
            || bytes.empty())
            continue;

        const CollisionShapeHandle handle = cache.LoadBlob(bytes);
        if (!handle.IsValid())
            continue;

        Transform3f transform;
        transform.Position = cell.Origin;

        const EntityId entity = world.CreateEntityWithSignature(
            partition,
            colliderSignature);
        *world.TryGet<LocalTransform>(entity) =
            LocalTransform{ transform };
        Collider collider;
        collider.Mesh = handle;
        *world.TryGet<Collider>(entity) = collider;
        ++loaded;
    }
    return loaded;
}
