// The preview viewports' orbit camera: framing, zoom limits, and the eye it
// places, checked as math with no device.

#include "viewport/OrbitCamera.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(OrbitCamera, FramingFitsTheSubjectAndKeepsTheViewAngle)
{
    OrbitCamera camera;
    camera.Yaw = 1.0f;
    camera.Pitch = 0.3f;
    camera.Frame(Vec3d(1.0f, 2.0f, 3.0f), 2.0f);

    EXPECT_FLOAT_EQ(camera.Distance, 6.0f);
    EXPECT_FLOAT_EQ(camera.Yaw, 1.0f) << "reframing spun the view";
    EXPECT_FLOAT_EQ(camera.Pitch, 0.3f);
    const Vec3d offset = camera.Eye() - Vec3d(1.0f, 2.0f, 3.0f);
    EXPECT_NEAR(offset.Magnitude(), 6.0f, 1e-4f);
    // Depth range scales with the subject, so a tiny rig and a huge one both
    // resolve their nearest and farthest surfaces.
    EXPECT_LT(camera.Near, 2.0f);
    EXPECT_GT(camera.Far, camera.Distance + 2.0f);
}

TEST(OrbitCamera, ZoomIsProportionalAndBounded)
{
    OrbitCamera camera;
    camera.Frame(Vec3d{}, 1.0f);
    const float start = camera.Distance;
    camera.Zoom(1.0f);
    const float oneStep = camera.Distance;
    EXPECT_LT(oneStep, start);
    camera.Zoom(1.0f);
    // Equal steps are equal ratios at any distance.
    EXPECT_NEAR(camera.Distance / oneStep, oneStep / start, 1e-5f);

    camera.Zoom(1000.0f);
    EXPECT_FLOAT_EQ(camera.Distance, camera.MinDistance);
    camera.Zoom(-1000.0f);
    EXPECT_FLOAT_EQ(camera.Distance, camera.MaxDistance);
}

TEST(OrbitCamera, PitchStopsShortOfThePoles)
{
    OrbitCamera camera;
    camera.Orbit(0.0f, 10.0f);
    EXPECT_LT(camera.Pitch, 1.5708f);
    camera.Orbit(0.0f, -20.0f);
    EXPECT_GT(camera.Pitch, -1.5708f);
    // A render built at the clamp is still finite.
    const CameraRenderData data = camera.BuildRenderData(16.0f / 9.0f);
    EXPECT_TRUE(std::isfinite(data.ViewProjection.Data[0][0]));
}
