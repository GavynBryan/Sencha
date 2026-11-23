#pragma once

#include <ecs/StoragePartitionId.h>
#include <math/Vec.h>

#include <functional>
#include <optional>

// World resource the loaded level installs: the partition of the resident zone a point belongs to, by
// LoadedLevel::ZoneAt; nullopt where no zone is resident. Empty with no level host.
struct ZonePlacement
{
    std::function<std::optional<StoragePartitionId>(const Vec3d&)> PartitionAt;
};
