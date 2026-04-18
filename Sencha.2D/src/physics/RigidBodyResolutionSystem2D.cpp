#include <physics/RigidBodyResolutionSystem2D.h>

#include <cmath>
#include <math/geometry/2d/Aabb2d.h>
#include <physics/PhysicsDomain2D.h>

RigidBodyResolutionSystem2D::RigidBodyResolutionSystem2D(
    TransformStore<Transform2f>& transforms,
    PhysicsDomain2D&            physics,
    RigidBodyStore&             bodies)
    : Transforms(transforms)
    , Physics(physics)
    , Bodies(bodies)
{}

void RigidBodyResolutionSystem2D::Tick(float fixedDt)
{
    const std::vector<Id>& owners = Bodies.GetOwners();
    std::span<RigidBody2D> bodies = Bodies.GetItems();

    for (size_t i = 0; i < bodies.size(); ++i)
    {
        RigidBody2D& body = bodies[i];
        const EntityId entity{ owners[i], 0 };
        if (body.Shape.IsStatic) continue;

        const Vec2d desiredDelta = { body.Velocity.X * fixedDt,
                                     body.Velocity.Y * fixedDt };
        body.Velocity = { 0.0f, 0.0f };

        if (std::abs(desiredDelta.X) < 1e-6f && std::abs(desiredDelta.Y) < 1e-6f)
        {
            body.LastHits = HitFlags2D::None;
            continue;
        }

        const Aabb2d& bounds = body.Shape.WorldBounds;
        if (!bounds.IsValid()) continue;

        const MoveResult2D result = Physics.MoveBox(bounds, desiredDelta, entity);
        body.LastHits = result.Hits;

        if (result.ResolvedDelta.X == 0.0f && result.ResolvedDelta.Y == 0.0f)
            continue;

        Transform2f* local = Transforms.TryGetLocalMutable(entity);
        if (!local) continue;

        local->Position.X += result.ResolvedDelta.X;
        local->Position.Y += result.ResolvedDelta.Y;
    }
}
