#include <framework/camera/CameraRig.h>

CameraPose ComputeCameraPose(const CameraRig& rig, const Vec3d& targetWorldPosition)
{
    CameraPose pose;
    if (rig.Mode == CameraRigMode::Fixed)
        return pose; // Override stays false: keep the authored camera pose.

    pose.Override = true;
    const Vec3d pivot = targetWorldPosition + rig.PivotOffset;
    pose.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), rig.Yaw)
                  * Quatf::FromAxisAngle(Vec3d::Right(), rig.Pitch);

    if (rig.Mode == CameraRigMode::FirstPerson)
        pose.Position = pivot;
    else
        pose.Position = pivot + pose.Rotation.RotateVector(Vec3d::Backward()) * rig.Distance;
    return pose;
}
