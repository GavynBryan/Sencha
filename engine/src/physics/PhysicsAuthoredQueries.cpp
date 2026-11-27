#include <physics/PhysicsAuthoredQueries.h>

#include <algorithm>
#include <span>

namespace
{
    PhysicsQueryFilter Filter(const std::optional<EntityId>& ignore)
    {
        return PhysicsQueryFilter{ .IncludeTriggers = true,
                                   .IgnoreEntities = ignore ? std::span(&*ignore, 1) : std::span<const EntityId>{} };
    }
}

std::optional<PhysicsTraceAnswer> PhysicsQueryOperations::Trace(Vec3d origin, Vec3d direction, float range,
                                                                std::optional<EntityId> ignore) const
{
    if (direction.SqrMagnitude() == 0.0)
        return std::nullopt;
    const RaycastHit hit = Physics.Raycast(origin, direction.Normalized(), range, Filter(ignore));
    return PhysicsTraceAnswer{ .Hit = hit.Hit, .Entity = hit.Entity, .Point = hit.Point, .Normal = hit.Normal };
}

std::vector<EntityId> PhysicsQueryOperations::Overlap(Vec3d position, float radius, std::optional<EntityId> ignore) const
{
    std::vector<EntityId> entities;
    Physics.OverlapShape(CollisionShape::MakeSphere(radius), position, Quatf::Identity(), entities, Filter(ignore));
    return entities;
}

bool PhysicsQueryOperations::ClearPath(Vec3d from, Vec3d to, float radius, float height,
                                       std::optional<EntityId> ignore) const
{
    const Vec3d delta = to - from;
    const float length = delta.Magnitude();
    if (length < 1.0e-4f)
        return true;
    const PhysicsQueryFilter filter{ .IncludeTriggers = false,
                                     .IgnoreEntities = ignore ? std::span(&*ignore, 1) : std::span<const EntityId>{},
                                     .IncludeCharacters = false };
    const Vec3d direction = delta / length;
    constexpr float kLineEndInset = 0.25f;
    if (radius > 0.0f)
        return !Physics.SweepShape(CollisionShape::MakeCapsule(radius, std::max(height * 0.5f - radius, 0.01f)), from,
                                   Quatf::Identity(), direction, length, filter)
                    .Hit;
    if (length > kLineEndInset)
        return !Physics.Raycast(from, direction, length - kLineEndInset, filter).Hit;
    return true;
}
