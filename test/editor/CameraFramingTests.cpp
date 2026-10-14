#include "WorkspaceFixture.h"

#include "viewport/CameraFraming.h"
#include "viewport/EditorCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

// Framing brings a box into view without turning the camera, and framing the
// selection frames everything selected, a light or an empty by its position.
namespace
{
float DistanceFromAxis(const EditorCamera& camera, Vec3d point)
{
    const Vec3d toPoint = point - camera.Position;
    const Vec3d forward = camera.GetForwardVector();
    const Vec3d along = forward * toPoint.Dot(forward);
    return (toPoint - along).Magnitude();
}
} // namespace

TEST(CameraFraming, APerspectiveCameraBacksOffUntilTheBoxFitsItsView)
{
    EditorCamera camera;
    camera.Yaw = 30.0f;
    camera.Pitch = -20.0f;
    const Vec3d forward = camera.GetForwardVector();
    const Aabb3d box = Aabb3d::FromCenterHalfExtent({ 10.0f, 0.0f, 4.0f }, { 2.0f, 2.0f, 2.0f });
    FrameBounds(camera, box);

    EXPECT_NEAR(camera.GetForwardVector().Dot(forward), 1.0f, 1e-5f) << "the camera is not turned";
    EXPECT_NEAR(DistanceFromAxis(camera, box.Center()), 0.0f, 1e-3f) << "the box is centred";
    const float radius = 0.5f * (box.Max - box.Min).Magnitude();
    const float halfFov = 0.5f * camera.FovY * std::numbers::pi_v<float> / 180.0f;
    EXPECT_GE((box.Center() - camera.Position).Magnitude() * std::sin(halfFov), radius) << "the whole box is seen";
}

TEST(CameraFraming, AnOrthographicCameraCentresAndZoomsToTheBox)
{
    EditorCamera camera;
    camera.ActiveMode = EditorCamera::Mode::Orthographic;
    const Vec3d position = camera.Position;
    const Aabb3d box = Aabb3d::FromMinMax({ 0.0f, 0.0f, 0.0f }, { 8.0f, 2.0f, 2.0f });
    FrameBounds(camera, box);

    EXPECT_EQ(camera.OrthoCenter, box.Center());
    EXPECT_GE(camera.OrthoHeight, 8.0f);
    EXPECT_EQ(camera.Position, position);
}

class FrameSelectionTest : public WorkspaceTest
{
};

TEST_F(FrameSelectionTest, ThePointOfAnEntityWithoutBoundsIsFramedWithTheRest)
{
    const EntityId brush = AddBrush({ 0.0f, 0.0f, 0.0f });
    const EntityId empty = Scene().CreateEntity({ 20.0f, 0.0f, 0.0f });
    Select({ SelectableRef::EntitySelection(Registry(), brush), SelectableRef::EntitySelection(Registry(), empty) });

    const std::optional<Aabb3d> spanned = Scene().SelectionWorldBounds(Workspace.Selection.GetSelection());
    ASSERT_TRUE(spanned.has_value());
    EXPECT_FLOAT_EQ(spanned->Max.X, 20.0f);
    EXPECT_FLOAT_EQ(spanned->Min.X, -0.5f);

    ASSERT_TRUE(Workspace.FrameSelection());
    const EditorCamera& camera = Workspace.Layout.Active()->Camera;
    EXPECT_NEAR(DistanceFromAxis(camera, spanned->Center()), 0.0f, 1e-3f);

    Workspace.Selection.ClearSelection();
    EXPECT_FALSE(Workspace.FrameSelection()) << "nothing selected frames nothing";
}
