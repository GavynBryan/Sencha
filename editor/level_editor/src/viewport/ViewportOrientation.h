#pragma once

#include "EditorCamera.h"
#include "GridSettings.h"

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
// `axisPlane` laid through `point`, with its lines in phase with the working
// grid's, so a snap on a surface lands on the same lattice as one on the grid.
GridPlane GridThrough(GridPlane axisPlane, Vec3d point, const GridSettings& grid);
