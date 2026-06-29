#pragma once

#include <cstdint>

#include <core/identity/StrongId.h>
#include <math/Vec.h>

// Handle into a CollisionShapeCache for a cooked triangle-mesh shape (static
// brush collision). Zero is invalid. Primitive colliders describe their shape
// inline below and leave this unset.
using CollisionShapeHandle = StrongId<struct CollisionShapeTag, uint32_t>;

//=============================================================================
// CollisionShape
//
// Backend-free description of a collision volume. PhysicsWorld translates it to
// the backend shape when a body is created. Primitives are described inline;
// cooked meshes (static brush collision) are referenced by handle and resolved
// through CollisionShapeCache (added with the cook path).
//=============================================================================

enum class CollisionShapeType : uint8_t
{
    Sphere = 0,
    Box = 1,
    Capsule = 2,
};

struct CollisionShape
{
    CollisionShapeType Type = CollisionShapeType::Box;

    // Box: half extents on each axis. Sphere/Capsule: Radius. Capsule: HalfHeight
    // is the half length of the cylindrical mid-section (caps are added on top),
    // so total height is 2 * (HalfHeight + Radius), matching the backend.
    Vec3d HalfExtents = Vec3d::One();
    float Radius = 0.5f;
    float HalfHeight = 0.5f;

    static CollisionShape MakeSphere(float radius)
    {
        CollisionShape s;
        s.Type = CollisionShapeType::Sphere;
        s.Radius = radius;
        return s;
    }

    static CollisionShape MakeBox(const Vec3d& halfExtents)
    {
        CollisionShape s;
        s.Type = CollisionShapeType::Box;
        s.HalfExtents = halfExtents;
        return s;
    }

    static CollisionShape MakeCapsule(float radius, float halfHeight)
    {
        CollisionShape s;
        s.Type = CollisionShapeType::Capsule;
        s.Radius = radius;
        s.HalfHeight = halfHeight;
        return s;
    }
};
