#include "viewport/CameraFraming.h"

#include "viewport/EditorCamera.h"

#include <math/Angle.h>

#include <algorithm>
#include <cmath>

namespace
{
// Room around the framed box, and the least radius framed, so a point (a light,
// an empty) is seen from a working distance rather than from inside it.
constexpr float kMargin = 1.15f;
constexpr float kMinRadius = 1.0f;
} // namespace

void FrameBounds(EditorCamera& camera, const Aabb3d& bounds)
{
    const Vec3d center = bounds.Center();
    const float radius = std::max(kMinRadius, 0.5f * (bounds.Max - bounds.Min).Magnitude()) * kMargin;
    if (camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        camera.OrthoCenter = center;
        camera.OrthoHeight = 2.0f * radius;
        return;
    }
    const float halfFov = ToRadians(0.5f * camera.FovY);
    camera.Position = center - camera.GetForwardVector() * (radius / std::sin(halfFov));
}
