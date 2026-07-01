#pragma once

#include <movement/MovementModes.h>
#include <movement/PlanarLocomotion.h>

#include <algorithm>

//=============================================================================
// Air locomotion: no friction, capped air-wish speed, for InAir characters.
//=============================================================================

namespace movement
{
    struct AirLocomotionPolicy
    {
        static void PreAccelerate(Vec3d&, const MovementProfile&, float)
        {
        }

        static float Acceleration(const MovementProfile& profile)
        {
            return profile.AirAcceleration;
        }

        static float WishSpeed(const MovementProfile& profile, float wishSpeed)
        {
            return std::min(wishSpeed, profile.MaxAirSpeed);
        }
    };
}

using AirLocomotionSystem =
    movement::PlanarLocomotionSystem<InAir, movement::AirLocomotionPolicy>;
