#include <physics/RigidBodySyncSystem2D.h>

#include <math/geometry/2d/Aabb2d.h>
#include <physics/PhysicsDomain2D.h>

RigidBodySyncSystem2D::RigidBodySyncSystem2D(TransformStore<Transform2f>& transforms,
                                             PhysicsDomain2D&            physics,
                                             RigidBodyStore&             bodies)
    : Transforms(transforms)
    , Physics(physics)
    , Bodies(bodies)
{}

void RigidBodySyncSystem2D::Tick(float /*fixedDt*/)
{
    const std::vector<Id>& owners = Bodies.GetOwners();
    std::span<RigidBody2D> bodies = Bodies.GetItems();

    for (size_t i = 0; i < bodies.size(); ++i)
    {
        RigidBody2D& body = bodies[i];
        const EntityHandle entity{ owners[i], 0 };
        const Transform2f* world = Transforms.TryGetWorld(entity);
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
        Bodies.EnsureDomainRegistration(entity, body);
        Physics.UpdateBounds(entity, worldBounds);
    }

    Physics.RebuildTree();
}
