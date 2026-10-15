#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

class World;

struct Aim
{
    Vec3d Origin = Vec3d::Zero();
    Vec3d Direction = Vec3d::Forward();
};

// Where `entity` aims from and toward: its position plus its AimOrigin turned with
// its yaw, along its LookOrientation. A body with no LookOrientation aims along its
// rotation.
[[nodiscard]] Aim AimOf(const World& world, EntityId entity);
