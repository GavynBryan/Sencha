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
// Velocities are the round-trip state: pushed into the body at bind, pulled
// back after every step for dynamic bodies. GravityScale is pushed every
// step, so a runtime edit (a held prop going weightless) takes effect on the
// next tick without a structural change. Damping is applied when the body is
// created; the backend exposes no cheap per-step setter for it. Logic lives
// in the physics systems; this struct holds no behavior.
//=============================================================================
struct RigidBody
{
    BodyMotion Motion = BodyMotion::Dynamic;
    float Mass = 1.0f; // <= 0 lets the backend derive mass from the shape
    Vec3d LinearVelocity = Vec3d::Zero();
    float GravityScale = 1.0f;

    Vec3d AngularVelocity = Vec3d::Zero(); // radians/second, world space

    // Velocity fraction removed per second, matching the backend's default so
    // a default component behaves exactly like a body without these fields.
    float LinearDamping = 0.05f;
    float AngularDamping = 0.05f;
};

SENCHA_DECLARE_COMPONENT_TYPE(RigidBody, "sencha.physics.rigid_body");
