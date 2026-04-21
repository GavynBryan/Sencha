#include "ViewportOrientation.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace
{
const std::array<OrientationTraits, 8> kTraits = {
    OrientationTraits{ "Perspective", EditorCamera::Mode::Perspective, {}, GridPlanes::XZ(), false },
    OrientationTraits{ "Top", EditorCamera::Mode::Orthographic, Vec3d(0.0f, 1.0f, 0.0f), GridPlanes::XZ(), false },
    OrientationTraits{ "Bottom", EditorCamera::Mode::Orthographic, Vec3d(0.0f, -1.0f, 0.0f), GridPlanes::XZ(), false },
    OrientationTraits{ "Front", EditorCamera::Mode::Orthographic, Vec3d(0.0f, 0.0f, -1.0f), GridPlanes::XY(), false },
    OrientationTraits{ "Back", EditorCamera::Mode::Orthographic, Vec3d(0.0f, 0.0f, 1.0f), GridPlanes::XY(), false },
    OrientationTraits{ "Left", EditorCamera::Mode::Orthographic, Vec3d(-1.0f, 0.0f, 0.0f), GridPlanes::YZ(), false },
    OrientationTraits{ "Right", EditorCamera::Mode::Orthographic, Vec3d(1.0f, 0.0f, 0.0f), GridPlanes::YZ(), false },
    OrientationTraits{ "User", EditorCamera::Mode::Orthographic, {}, GridPlanes::XZ(), true },
};

const std::array<ViewportOrientation, 8> kOrientations = {
    ViewportOrientation::Perspective,
    ViewportOrientation::Top,
    ViewportOrientation::Bottom,
    ViewportOrientation::Front,
    ViewportOrientation::Back,
    ViewportOrientation::Left,
    ViewportOrientation::Right,
    ViewportOrientation::User,
};
}

const OrientationTraits& Traits(ViewportOrientation orientation)
{
    return kTraits[static_cast<size_t>(orientation)];
}

const std::array<ViewportOrientation, 8>& AllViewportOrientations()
{
    return kOrientations;
}

GridPlane GridForAxis(const Vec3d& axis)
{
    const float absX = std::abs(axis.X);
    const float absY = std::abs(axis.Y);
    const float absZ = std::abs(axis.Z);

    if (absY >= absX && absY >= absZ)
        return GridPlanes::XZ();
    if (absX >= absY && absX >= absZ)
        return GridPlanes::YZ();
    return GridPlanes::XY();
}
