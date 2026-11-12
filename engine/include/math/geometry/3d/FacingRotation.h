#pragma once

#include <math/Quat.h>
#include <math/Vec.h>

#include <algorithm>
#include <cmath>
#include <numbers>

// The rotation that turns forward (+Z) onto a unit `direction`. Direction alone
// does not fix roll, so the roll is whichever the shortest arc gives.
[[nodiscard]] inline Quatf FacingRotation(const Vec3d& direction)
{
    const Vec3d forward = Vec3d::Forward();
    const float cosine = std::clamp(forward.Dot(direction), -1.0f, 1.0f);
    const Vec3d axis = forward.Cross(direction);
    if (axis.SqrMagnitude() > 1.0e-8f)
        return Quatf::FromAxisAngle(axis.Normalized(), std::acos(cosine));
    return cosine < 0.0f ? Quatf::FromAxisAngle(Vec3d::Up(), std::numbers::pi_v<float>) : Quatf::Identity();
}
