#include <camera/CameraViewOffset.h>

#include <math/Angle.h>

Transform3f CameraViewOffsetTransform(const CameraViewOffset& offset)
{
    Transform3f local = Transform3f::Identity();
    local.Position = offset.Translation;
    local.Rotation = Quatf::FromPitchYawRoll(ToRadians(offset.Rotation.X), ToRadians(offset.Rotation.Y),
                                             ToRadians(offset.Rotation.Z));
    return local;
}

Transform3f ApplyCameraViewOffset(const Transform3f& pose, const CameraViewOffset& offset)
{
    if (offset.IsIdentity())
        return pose;
    const Transform3f local = CameraViewOffsetTransform(offset);
    Transform3f moved = pose;
    moved.Position = pose.Position + pose.Rotation.RotateVector(local.Position);
    moved.Rotation = (pose.Rotation * local.Rotation).Normalized();
    return moved;
}
