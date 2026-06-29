#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>
#include <physics/PhysicsTypes.h>

//=============================================================================
// RigidBody
//
// Data-only ECS component. Marks an entity's collider as participating in
// dynamics and carries its motion parameters. An entity with a Collider but no
// RigidBody is treated as static world geometry.
//
// Phase 1 is linear-only: angular motion is not integrated yet. Logic lives in
// the physics systems; this struct holds no behavior.
//=============================================================================
struct RigidBody
{
    BodyMotion Motion = BodyMotion::Dynamic;
    float Mass = 1.0f; // <= 0 lets the backend derive mass from the shape
    Vec3d LinearVelocity = Vec3d::Zero();
    float GravityScale = 1.0f;
};

SENCHA_DECLARE_COMPONENT_TYPE(RigidBody, "sencha.physics.rigid_body");
