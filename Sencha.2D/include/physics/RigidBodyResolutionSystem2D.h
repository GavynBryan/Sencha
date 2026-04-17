#pragma once

#include <math/geometry/2d/Transform2d.h>
#include <physics/RigidBody2D.h>
#include <transform/TransformStore.h>

class PhysicsDomain2D;

//=============================================================================
// RigidBodyResolutionSystem2D
//
// Fixed-lane system that applies kinematic movement for all non-static bodies.
// Reads each body's Velocity (world units/sec), multiplies by fixedDt to get
// the desired delta, calls MoveBox against the registered static blockers,
// writes the resolved delta into the body's local transform, and stores
// contact flags in LastHits. Velocity is reset to zero after each step —
// gameplay re-writes it each frame to express intent.
//
// Only dynamic bodies are resolved. Static bodies (Shape.IsStatic) are
// blockers registered in the domain by RigidBodySyncSystem2D and are not
// moved here.
//
// Ordering (via After<> in PhysicsSetup2D::Setup):
//   RigidBodySyncSystem2D -> RigidBodyResolutionSystem2D
//=============================================================================
class RigidBodyResolutionSystem2D
{
public:
    RigidBodyResolutionSystem2D(TransformStore<Transform2f>& transforms,
                                PhysicsDomain2D&            physics,
                                RigidBodyStore&             bodies);

    void Tick(float fixedDt);

private:
    TransformStore<Transform2f>& Transforms;
    PhysicsDomain2D&            Physics;
    RigidBodyStore&             Bodies;
};
