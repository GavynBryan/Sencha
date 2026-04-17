#include <physics/2d/RigidBodyResolutionSystem2D.h>

#include <cmath>
#include <math/geometry/2d/Aabb2d.h>
#include <physics/2d/PhysicsDomain2D.h>

RigidBodyResolutionSystem2D::RigidBodyResolutionSystem2D(
    TransformStore<Transform2f>& transforms,
    PhysicsDomain2D&            physics,
    DataBatch<RigidBody2D>&     bodies)
    : Transforms(transforms)
    , Physics(physics)
    , Bodies(bodies)
{}

void RigidBodyResolutionSystem2D::Tick(float fixedDt)
{
    for (RigidBody2D& body : Bodies.GetItems())
    {
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

        const MoveResult2D result = Physics.MoveBox(bounds, desiredDelta, body.DomainHandle);
        body.LastHits = result.Hits;

        if (result.ResolvedDelta.X == 0.0f && result.ResolvedDelta.Y == 0.0f)
            continue;

        Transform2f* local = Transforms.TryGetLocalMutable(body.Entity);
        if (!local) continue;

        local->Position.X += result.ResolvedDelta.X;
        local->Position.Y += result.ResolvedDelta.Y;
    }
}
