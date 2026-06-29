#pragma once

// Internal Jolt-side completion of CollisionShapeCache's PIMPL. Holds the loaded
// shapes; handle.Value - 1 indexes (0 is the invalid handle).

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <vector>

#include <physics/CollisionShape.h>

struct CollisionShapeCacheImpl
{
    std::vector<JPH::ShapeRefC> Shapes;

    [[nodiscard]] JPH::ShapeRefC Resolve(CollisionShapeHandle handle) const
    {
        if (handle.Value == 0 || handle.Value > Shapes.size())
            return JPH::ShapeRefC();
        return Shapes[handle.Value - 1];
    }
};
