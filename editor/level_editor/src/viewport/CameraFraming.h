#pragma once

#include <math/geometry/3d/Aabb3d.h>

struct EditorCamera;

// Brings a box into view without turning the camera: a perspective camera backs
// off along its own forward until the box's bounding sphere fits the vertical
// field of view; an orthographic one centres on it and zooms to its size.
void FrameBounds(EditorCamera& camera, const Aabb3d& bounds);
