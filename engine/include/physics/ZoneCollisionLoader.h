#pragma once

#include <ecs/StoragePartitionId.h>

#include <string>

class CollisionShapeCache;
class World;

// Loads cooked brush collision into the requested storage partition. Zone
// import passes its hidden partition so collision becomes visible atomically
// with the rest of the zone. Partition zero remains the explicit default for
// world-lifetime collision.
int LoadZoneCollision(
    World& world,
    CollisionShapeCache& cache,
    const std::string& sidecarPath,
    const std::string& cookedRoot,
    StoragePartitionId partition = StoragePartitionId::Default());
