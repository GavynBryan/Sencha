#pragma once

#include <math/geometry/3d/Transform3d.h>

// How far a view is moved from its camera, in the camera's own space: degrees of
// pitch, yaw and roll, and metres right, up and back. Every presented effect that
// moves the view produces one, and they add.
struct CameraViewOffset
{
    Vec3d Rotation = Vec3d::Zero();
    Vec3d Translation = Vec3d::Zero();

    [[nodiscard]] bool IsIdentity() const { return Rotation == Vec3d::Zero() && Translation == Vec3d::Zero(); }

    CameraViewOffset& operator+=(const CameraViewOffset& other)
    {
        Rotation += other.Rotation;
        Translation += other.Translation;
        return *this;
    }
};

// The offset as a pose relative to the camera.
[[nodiscard]] Transform3f CameraViewOffsetTransform(const CameraViewOffset& offset);

// The pose a camera is drawn from once `offset` moves it.
[[nodiscard]] Transform3f ApplyCameraViewOffset(const Transform3f& pose, const CameraViewOffset& offset);
