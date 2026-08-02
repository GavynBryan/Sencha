#pragma once

#include <math/Vec.h>

#include <algorithm>

//=============================================================================
// Movement step math (pure)
//
// The per-entity locomotion kernel, free of World/ECS/app so it is trivially
// unit-testable. Acceleration is projected onto the wish direction: it only
// tops up the velocity component along wishDir, up to wishSpeed, which
// preserves existing momentum and, with a low wish-speed cap, leaves steering
// authority without letting the character accelerate freely. Friction is a
// multiplicative decay with a stop-speed floor so low speeds stop crisply
// instead of asymptoting.
//
// Both operate on the caller's chosen plane; the axis split belongs to the
// system that calls these.
//=============================================================================
namespace movement
{
    inline constexpr float kEpsilon = 1e-6f;

    // Multiplicative friction decay toward rest, floored by stopSpeed so slow
    // speeds bleed off at a fixed rate rather than asymptoting.
    inline void ApplyFriction(Vec3d& velocity, float friction, float stopSpeed, float dt)
    {
        const float speed = velocity.Magnitude();
        if (speed < kEpsilon)
        {
            velocity = Vec3d::Zero();
            return;
        }
        const float control = speed < stopSpeed ? stopSpeed : speed;
        const float newSpeed = std::max(0.0f, speed - control * friction * dt);
        velocity *= newSpeed / speed;
    }

    // Add speed along wishDir (unit length) up to (wishSpeed - v.wishDir).
    inline void Accelerate(Vec3d& velocity, const Vec3d& wishDir, float wishSpeed,
                           float accel, float dt)
    {
        const float add = wishSpeed - velocity.Dot(wishDir);
        if (add <= 0.0f)
            return;
        const float step = std::min(accel * wishSpeed * dt, add);
        velocity += wishDir * step;
    }
}
