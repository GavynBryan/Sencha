#include <physics/CollisionShape.h>

#include <cmath>

bool IsValidCollisionShape(const CollisionShape& shape)
{
    switch (shape.Type)
    {
    case CollisionShapeType::Sphere:
        return std::isfinite(shape.Radius) && shape.Radius > 0;
    case CollisionShapeType::Box:
        return std::isfinite(shape.HalfExtents.X) && std::isfinite(shape.HalfExtents.Y)
            && std::isfinite(shape.HalfExtents.Z) && shape.HalfExtents.X > 0
            && shape.HalfExtents.Y > 0 && shape.HalfExtents.Z > 0;
    case CollisionShapeType::Capsule:
        return std::isfinite(shape.Radius) && shape.Radius > 0
            && std::isfinite(shape.HalfHeight) && shape.HalfHeight >= 0;
    }
    return false;
}
