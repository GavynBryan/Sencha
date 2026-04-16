#pragma once

#include <math/Vec.h>

//=============================================================================
// CharacterMotor2D
//
// Per-entity kinematic movement state for v0 2D character physics.
// Owned by gameplay and written by PlayerMotorSystem2D; consumed by
// KinematicMoveSystem2D to produce a resolved movement delta each frame.
//
// v0 scope: gravity, jump, horizontal movement, grounded detection.
// Not a rigidbody — no forces, torque, or impulses.
//=============================================================================
struct CharacterMotor2D
{
    // -- Tuning parameters (set at creation, may be tweaked at runtime) -------

    float MoveSpeed  = 5.0f;   // Horizontal travel speed (units/sec)
    float JumpSpeed  = 8.0f;   // Vertical velocity applied on jump
    float Gravity    = 20.0f;  // Downward acceleration (units/sec²)

    // -- Per-frame desired input (written by PlayerMotorSystem2D) -------------

    float DesiredMoveX = 0.0f; // -1..1 horizontal input axis

    // -- Integrated state (maintained across frames) --------------------------

    float VerticalSpeed = 0.0f; // Current vertical velocity (+ = up)
    bool  Grounded      = false; // True when standing on a floor surface

    // -- Jump request (set by PlayerMotorSystem2D, consumed by KinematicMoveSystem2D) --

    bool JumpRequested = false;
};
