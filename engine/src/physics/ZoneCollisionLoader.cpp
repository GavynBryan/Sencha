#include <physics/ZoneCollisionLoader.h>

#include <cstddef>
#include <fstream>
#include <ios>
#include <vector>

#include <ecs/ArchetypeSignature.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <physics/CollisionShapeCache.h>
#include <physics/components/Collider.h>
#include <world/transform/TransformComponents.h>

namespace
{
std::vector<std::byte> ReadFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return {};

    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return {};
    return bytes;
}
} // namespace

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
        const std::vector<std::byte> bytes =
            ReadFileBytes(cookedRoot + "/" + cell.BlobPath);
        if (bytes.empty())
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
