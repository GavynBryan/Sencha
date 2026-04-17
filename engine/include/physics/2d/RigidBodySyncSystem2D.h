#pragma once

#include <core/batch/DataBatch.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/2d/RigidBody2D.h>
#include <world/transform/TransformStore.h>

class PhysicsDomain2D;

//=============================================================================
// RigidBodySyncSystem2D
//
// Fixed-lane system that bridges the transform domain and the physics domain
// each step.
//
// For every live body it reads the current world Transform2f and computes the
// AABB from the body's local-space shape, storing it in Shape.WorldBounds.
//
// Static bodies (Shape.IsStatic) are also registered in PhysicsDomain2D so
// they participate as blockers in MoveBox / SweepBox queries. Registration is
// deferred to the first tick — bodies emplaced mid-session are picked up
// automatically. Dynamic bodies are never registered in the domain; they are
// movers that query it, not obstacles inside it.
//
// Calls RebuildTree once after all bounds are current.
//
// Ordering (declared in PhysicsSetup2D::Setup via After<>):
//   TransformPropagationSystem
//     -> RigidBodySyncSystem2D
//       -> RigidBodyResolutionSystem2D
//=============================================================================
class RigidBodySyncSystem2D
{
public:
    RigidBodySyncSystem2D(TransformStore<Transform2f>& transforms,
                          PhysicsDomain2D&            physics,
                          DataBatch<RigidBody2D>&     bodies);

    void Tick(float fixedDt);

private:
    TransformStore<Transform2f>& Transforms;
    PhysicsDomain2D&            Physics;
    DataBatch<RigidBody2D>&     Bodies;
};
