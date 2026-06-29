#pragma once

#include <vector>

#include <ecs/EntityId.h>
#include <math/Quat.h>
#include <math/Vec.h>
#include <physics/CollisionShape.h>

class PhysicsWorld;

//=============================================================================
// PhysicsQueries
//
// The read-only spatial-query surface for gameplay: raycasts, shape sweeps, and
// overlaps over a PhysicsWorld. A lightweight non-owning view, so a system that
// only needs queries depends on this and cannot step the simulation or mutate
// bodies (the responsibility split from PhysicsWorld). Hits report the owning
// EntityId, recovered from the body's user data, not a backend body id.
//=============================================================================

struct RaycastHit
{
    bool Hit = false;
    EntityId Entity;
    float Distance = 0.0f;
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
};

struct ShapeSweepHit
{
    bool Hit = false;
    EntityId Entity;
    float Fraction = 1.0f; // 0..1 along the swept displacement
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
};

class PhysicsQueries
{
public:
    explicit PhysicsQueries(const PhysicsWorld& world)
        : Simulation(&world)
    {
    }

    // Closest body hit by the ray, or { Hit = false } if none within maxDistance.
    [[nodiscard]] RaycastHit Raycast(const Vec3d& origin, const Vec3d& direction, float maxDistance) const;

    // Closest body hit while sweeping shape from origin along direction*maxDistance.
    [[nodiscard]] ShapeSweepHit SweepShape(
        const CollisionShape& shape,
        const Vec3d& origin,
        const Quatf& rotation,
        const Vec3d& direction,
        float maxDistance) const;

    // Entities whose bodies overlap shape placed at (position, rotation). Each
    // entity appears once. Cleared before filling.
    void OverlapShape(
        const CollisionShape& shape,
        const Vec3d& position,
        const Quatf& rotation,
        std::vector<EntityId>& out) const;

private:
    const PhysicsWorld* Simulation;
};
