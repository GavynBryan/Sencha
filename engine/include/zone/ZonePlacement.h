#pragma once

#include <ecs/StoragePartitionId.h>
#include <math/Vec.h>

#include <functional>
#include <optional>

// World resource LoadedLevel installs: the partition of LoadedLevel::ZoneAt, nullopt where it is not resident.
struct ZonePlacement
{
    std::function<std::optional<StoragePartitionId>(const Vec3d&)> PartitionAt;
};
