// Picking a joint in the viewport: joints projected through the preview's own
// orbit camera, and a click resolved to the nearest marker within reach.

#include "authoring/AnimationJointPicking.h"

#include "viewport/OrbitCamera.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    Mat4 At(float x, float y, float z)
    {
        Mat4 m = Mat4::Identity();
        m.Data[0][3] = x;
        m.Data[1][3] = y;
        m.Data[2][3] = z;
        return m;
    }
}

TEST(AnimationJointPicking, JointsLandWhereTheCameraSeesThem)
{
    OrbitCamera camera;
    camera.Yaw = 0.0f;
    camera.Pitch = 0.0f;
    camera.Frame(Vec3d(0.0f, 0.0f, 0.0f), 1.0f);
    const CameraRenderData view = camera.BuildRenderData(1.0f);

    const std::vector<Mat4> model{ At(0.0f, 0.0f, 0.0f), At(0.0f, 0.5f, 0.0f), At(0.0f, -0.5f, 0.0f) };
    const std::vector<AnimationJointMarker> markers = ProjectAnimationJoints(model, view.ViewProjection, 400.0f, 400.0f);
    ASSERT_EQ(markers.size(), 3u);
    // The target is the viewport's middle; up in the world is up on screen.
    EXPECT_NEAR(markers[0].X, 200.0f, 0.5f);
    EXPECT_NEAR(markers[0].Y, 200.0f, 0.5f);
    EXPECT_LT(markers[1].Y, markers[0].Y);
    EXPECT_GT(markers[2].Y, markers[0].Y);

    // A joint behind the eye has no marker.
    const Vec3d eye = camera.Eye();
    const Vec3d behind = eye + (eye - camera.Target);
    EXPECT_TRUE(ProjectAnimationJoints(std::vector<Mat4>{ At(behind.X, behind.Y, behind.Z) }, view.ViewProjection,
                                       400.0f, 400.0f)
                    .empty());
}

TEST(AnimationJointPicking, AClickPicksTheNearestMarkerWithinReach)
{
    const std::vector<AnimationJointMarker> markers{
        { 0, 100.0f, 100.0f, 0.5f }, { 1, 104.0f, 100.0f, 0.5f }, { 2, 300.0f, 300.0f, 0.2f }, { 3, 300.0f, 300.0f, 0.1f } };
    EXPECT_EQ(PickAnimationJoint(markers, 101.0f, 100.0f, 8.0f), 0u);
    EXPECT_EQ(PickAnimationJoint(markers, 103.5f, 100.0f, 8.0f), 1u);
    EXPECT_EQ(PickAnimationJoint(markers, 300.0f, 301.0f, 8.0f), 3u) << "stacked: the one nearer the eye";
    EXPECT_FALSE(PickAnimationJoint(markers, 200.0f, 200.0f, 8.0f).has_value());
}
