#pragma once

#include "EditorCamera.h"

#include <math/spatial/GridPlane.h>

#include <array>
#include <cstdint>

enum class ViewportOrientation : uint8_t
{
    Perspective,
    Top,
    Bottom,
    Front,
    Back,
    Left,
    Right,
    User
};

struct OrientationTraits
{
    const char* Label = "";
    EditorCamera::Mode Mode = EditorCamera::Mode::Perspective;
    Vec3d OrthoAxis = {};
    GridPlane Grid = GridPlanes::XZ();
    bool UsesCameraAxis = false;
};

const OrientationTraits& Traits(ViewportOrientation orientation);
const std::array<ViewportOrientation, 8>& AllViewportOrientations();
GridPlane GridForAxis(const Vec3d& axis);
