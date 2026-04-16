#pragma once

#include <math/Vec.h>

//=============================================================================
// RigidBody2D  [SCAFFOLDING — not used in v0]
//
// Placeholder for a future full-simulation rigidbody. Left empty so type
// exists and code can reference it without enabling simulation.
//
// v0 explicitly does not support: forces, torque, impulses, or
// dynamic-dynamic collision. Add fields and wire into PhysicsDomain2D
// when simulation is needed.
//=============================================================================
struct RigidBody2D
{
    // Reserved for future use.
};

//=============================================================================
// LinearVelocity2D  [SCAFFOLDING — not used in v0]
//=============================================================================
struct LinearVelocity2D
{
    Vec2d Value = { 0.0f, 0.0f };
};

//=============================================================================
// AngularVelocity2D  [SCAFFOLDING — not used in v0]
//=============================================================================
struct AngularVelocity2D
{
    float Value = 0.0f; // Radians per second
};
