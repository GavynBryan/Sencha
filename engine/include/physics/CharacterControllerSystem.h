#pragma once

#include <math/Vec.h>

struct PhysicsContext;
class PhysicsStepSystem;

//=============================================================================
// CharacterControllerSystem
//
// Drives one CharacterMover per CharacterController entity against the shared
// simulation. Scheduled in the Physics step, after PhysicsStepSystem so the
// character collides against the stepped world (declare the order with
// Schedule.After<CharacterControllerSystem, PhysicsStepSystem>()). Reads each
// entity's authored capsule + DesiredVelocity, advances the mover, and writes
// the resolved position to LocalTransform and Grounded back to the component.
//=============================================================================
class CharacterControllerSystem
{
public:
    explicit CharacterControllerSystem(PhysicsStepSystem& step);

    void Physics(PhysicsContext& ctx);

private:
    PhysicsStepSystem* Step;
    Vec3d Gravity = Vec3d(0.0f, -9.81f, 0.0f);
};
