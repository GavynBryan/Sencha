// Which camera a viewport renders through.
//
// A viewport carries two: the one its panel rect implies, which the input path
// projects picks against, and the one its render target implies. They are the
// same number except during a resize, when the target still holds the size the
// panel reported last frame -- and that one frame is exactly when rendering
// through the wrong one is visible, as a stretched image under the cursor.
//
// Every renderer inside a view used to derive its own camera from the panel
// rect, which agreed only because the render path overwrote that rect with the
// target's size for the duration of the pass. This pins the value they are all
// handed now instead.

#include <gtest/gtest.h>

#include "viewport/EditorViewport.h"

namespace
{

EditorViewport PerspectiveViewport()
{
    EditorViewport viewport;
    viewport.ApplyOrientation(ViewportOrientation::Perspective);
    viewport.Camera.Position = Vec3d(0.0f, 2.0f, 5.0f);
    return viewport;
}

} // namespace

TEST(ViewportRenderCamera, MatchesThePanelCameraWhenTheTargetMatchesThePanel)
{
    EditorViewport viewport = PerspectiveViewport();
    viewport.RegionMin = ImVec2(10.0f, 20.0f);
    viewport.RegionMax = ImVec2(810.0f, 620.0f); // 800x600

    const CameraRenderData panel = viewport.BuildRenderData();
    const CameraRenderData target = viewport.CameraForExtent(800, 600);

    EXPECT_EQ(panel.Projection, target.Projection);
    EXPECT_EQ(panel.View, target.View);
}

TEST(ViewportRenderCamera, FollowsTheTargetRatherThanThePanelWhenTheyDisagree)
{
    EditorViewport viewport = PerspectiveViewport();
    // Mid-drag: the panel is already square, the target is still 2:1 from the
    // size the panel reported last frame.
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(400.0f, 400.0f);

    const CameraRenderData target = viewport.CameraForExtent(400, 200);
    const CameraRenderData square = viewport.CameraForExtent(400, 400);

    EXPECT_FALSE(target.Projection == square.Projection);
    // What is rendered is what the target's own aspect implies.
    EXPECT_EQ(target.Projection, viewport.Camera.BuildRenderData(2.0f).Projection);
    EXPECT_FALSE(target.Projection == viewport.BuildRenderData().Projection);
}

TEST(ViewportRenderCamera, TreatsADegenerateExtentAsSquareRatherThanDividingByZero)
{
    EditorViewport viewport = PerspectiveViewport();
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(400.0f, 400.0f);

    const CameraRenderData square = viewport.CameraForExtent(400, 400);
    EXPECT_EQ(viewport.CameraForExtent(0, 0).Projection, square.Projection);
    EXPECT_EQ(viewport.CameraForExtent(800, 0).Projection, square.Projection);
}

TEST(ViewportRenderCamera, AnOrthographicViewportSizesItsBoxFromTheTargetToo)
{
    EditorViewport viewport;
    viewport.ApplyOrientation(ViewportOrientation::Top);
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(400.0f, 400.0f);

    const CameraRenderData wide = viewport.CameraForExtent(800, 400);
    EXPECT_EQ(wide.Projection, viewport.Camera.BuildRenderData(2.0f).Projection);
    EXPECT_FALSE(wide.Projection == viewport.BuildRenderData().Projection);
}
