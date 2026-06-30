#include <gtest/gtest.h>

#include <framework/camera/CameraRig.h>

TEST(CameraPose, FirstPersonSitsAtPivot)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;
    rig.PivotOffset = Vec3d(0.0f, 1.6f, 0.0f);

    const CameraPose pose = ComputeCameraPose(rig, Vec3d(5.0f, 0.0f, 3.0f));

    EXPECT_TRUE(pose.Override);
    EXPECT_FLOAT_EQ(pose.Position.X, 5.0f);
    EXPECT_FLOAT_EQ(pose.Position.Y, 1.6f);
    EXPECT_FLOAT_EQ(pose.Position.Z, 3.0f);
}

TEST(CameraPose, ThirdPersonPlacesBoomBehindAtRest)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::ThirdPerson;
    rig.PivotOffset = Vec3d(0.0f, 1.0f, 0.0f);
    rig.Distance = 4.0f;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d::Zero());

    // At yaw 0 / pitch 0 the look direction is -Z, so the boom (behind) is +Z.
    EXPECT_TRUE(pose.Override);
    EXPECT_NEAR(pose.Position.X, 0.0f, 1e-4f);
    EXPECT_NEAR(pose.Position.Y, 1.0f, 1e-4f);
    EXPECT_NEAR(pose.Position.Z, 4.0f, 1e-4f);
}

TEST(CameraPose, ThirdPersonPreservesBoomLength)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::ThirdPerson;
    rig.PivotOffset = Vec3d::Zero();
    rig.Distance = 4.0f;
    rig.Yaw = 0.9f;
    rig.Pitch = 0.2f;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d::Zero());

    EXPECT_NEAR(pose.Position.Magnitude(), rig.Distance, 1e-3f);
}

TEST(CameraPose, FixedLeavesAuthoredPose)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::Fixed;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d(9.0f, 9.0f, 9.0f));

    EXPECT_FALSE(pose.Override);
}
