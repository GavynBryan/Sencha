#include <physics/CollisionShape.h>

#include <algorithm>
#include <cmath>

CollisionShape ScaledCollisionShape(const CollisionShape& shape, const Vec3d& scale)
{
    const Vec3d magnitude(std::abs(scale.X), std::abs(scale.Y), std::abs(scale.Z));
    if (magnitude == Vec3d::One())
        return shape;

    CollisionShape scaled = shape;
    scaled.Center = Vec3d(shape.Center.X * scale.X, shape.Center.Y * scale.Y, shape.Center.Z * scale.Z);
    // The scale as the shape's own axes see it.
    const Vec3d turned = shape.Orientation == Quatf::Identity()
        ? magnitude
        : shape.Orientation.Inverse().RotateVector(magnitude);
    const Vec3d own(std::abs(turned.X), std::abs(turned.Y), std::abs(turned.Z));
    switch (shape.Type)
    {
    case CollisionShapeType::Box:
        scaled.HalfExtents = Vec3d(shape.HalfExtents.X * own.X, shape.HalfExtents.Y * own.Y,
                                   shape.HalfExtents.Z * own.Z);
        break;
    case CollisionShapeType::Sphere:
        scaled.Radius = shape.Radius * std::max({ magnitude.X, magnitude.Y, magnitude.Z });
        break;
    case CollisionShapeType::Capsule:
        scaled.Radius = shape.Radius * std::max(own.X, own.Z);
        scaled.HalfHeight = shape.HalfHeight * own.Y;
        break;
    }
    return scaled;
}
