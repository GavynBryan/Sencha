#pragma once

#include <ecs/StoragePartitionId.h>
#include <world/scene/SmapFormat.h>

#include <span>
#include <string>

class CollisionShapeCache;
class World;

// Loads cooked brush collision into the requested storage partition, from the
// collision cells a scene's .smap carries. Zone import passes its hidden
// partition so collision becomes visible atomically with the rest of the zone.
// Partition zero remains the explicit default for world-lifetime collision.
int LoadZoneCollision(
    World& world,
    CollisionShapeCache& cache,
    std::span<const SmapCollisionCell> cells,
    const std::string& cookedRoot,
    StoragePartitionId partition = StoragePartitionId::Default());
