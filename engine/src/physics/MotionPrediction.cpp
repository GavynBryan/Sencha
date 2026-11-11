#include <physics/MotionPrediction.h>

#include <physics/PhysicsQueries.h>

#include <algorithm>
#include <array>

Vec3d PredictMotion(const PhysicsQueries& physics, const Vec3d& from, const Vec3d& velocity, float seconds,
                    EntityId subject, const MotionPredictionShape& shape)
{
    const Vec3d planar(velocity.X, 0.0f, velocity.Z);
    const float speed = planar.Magnitude();
    if (speed < 0.1f || seconds <= 0.0f || shape.Steps <= 0)
        return from;
    const Vec3d direction = planar / speed;
    const float step = speed * seconds / static_cast<float>(shape.Steps);
    const std::array<EntityId, 1> ignored{ subject };
    const PhysicsQueryFilter filter{ .IncludeTriggers = false, .IgnoreEntities = ignored, .IncludeCharacters = false };
    Vec3d at = from;
    for (int index = 0; index < shape.Steps; ++index)
    {
        const RaycastHit wall = physics.Raycast(at, direction, step + shape.BodyRadius, filter);
        if (wall.Hit)
            return at + direction * std::max(wall.Distance - shape.BodyRadius, 0.0f);
        const Vec3d next = at + direction * step;
        if (!physics.Raycast(next, -Vec3d::Up(), shape.GroundBelow, filter).Hit)
            return at;
        at = next;
    }
    return at;
}
