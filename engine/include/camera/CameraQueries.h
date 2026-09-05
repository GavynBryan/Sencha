#pragma once

#include <ecs/EntityId.h>
#include <ecs/World.h>

// The first entity in `partition` carrying a CameraComponent, or invalid when
// there is none. "First" is alive-entity order, which is what a level with one
// authored camera needs and all a level with several is promised.
[[nodiscard]] EntityId FirstAuthoredCamera(const World& world,
                                           StoragePartitionId partition);
