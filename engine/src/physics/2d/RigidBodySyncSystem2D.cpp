#include <physics/2d/RigidBodySyncSystem2D.h>

#include <math/geometry/2d/Aabb2d.h>
#include <physics/2d/PhysicsDomain2D.h>

RigidBodySyncSystem2D::RigidBodySyncSystem2D(TransformView<Transform2f>& transforms,
                                             PhysicsDomain2D&            physics,
                                             DataBatch<RigidBody2D>&     bodies)
    : Transforms(transforms)
    , Physics(physics)
    , Bodies(bodies)
{}

void RigidBodySyncSystem2D::Tick(float /*fixedDt*/)
{
    for (RigidBody2D& body : Bodies.GetItems())
    {
        const Transform2f* world = Transforms.TryGetWorld(body.TransformKey);
        if (!world) continue;

        const Vec2d scaledHalf = {
            body.Shape.HalfExtent.X * world->Scale.X,
            body.Shape.HalfExtent.Y * world->Scale.Y
        };
        const Vec2d center = {
            world->Position.X + body.Shape.Offset.X,
            world->Position.Y + body.Shape.Offset.Y
        };

        const Aabb2d worldBounds = Aabb2d::FromCenterHalfExtent(center, scaledHalf);
        body.Shape.WorldBounds = worldBounds;
        Physics.UpdateBounds(body.DomainHandle, worldBounds);
    }

    Physics.RebuildTree();
}
