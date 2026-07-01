#pragma once

#include <movement/MovementModes.h>
#include <movement/PlanarLocomotion.h>

//=============================================================================
// Ground locomotion: friction-based planar movement for OnGround characters.
//=============================================================================

namespace movement
{
    struct GroundLocomotionPolicy
    {
        static void PreAccelerate(Vec3d& velocity, const MovementProfile& profile, float dt)
        {
            ApplyFriction(velocity, profile.Friction, profile.StopSpeed, dt);
        }

        static float Acceleration(const MovementProfile& profile)
        {
            return profile.GroundAcceleration;
        }

        static float WishSpeed(const MovementProfile&, float wishSpeed)
        {
            return wishSpeed;
        }
    };
}

using GroundLocomotionSystem =
    movement::PlanarLocomotionSystem<OnGround, movement::GroundLocomotionPolicy>;
