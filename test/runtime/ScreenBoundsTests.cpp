// The one rule for a scissor built from world bounds, which texture projection and
// outlines both follow: wholly behind the eye covers nothing, straddling it covers
// the whole view.

#include <gtest/gtest.h>

#include <render/CameraProjection.h>
#include <render/ScreenBounds.h>

namespace
{
// The eye at the origin looking down -Z.
Mat4 ViewProjection()
{
    return MakeVulkanPerspective(1.2f, 16.0f / 9.0f, 0.1f, 100.0f);
}

Aabb3d Box(Vec3d min, Vec3d max)
{
    Aabb3d box;
    box.Min = min;
    box.Max = max;
    return box;
}
}

TEST(ScreenBounds, ABoxInFrontLandsInsideTheView)
{
    const NdcBounds bounds = ProjectToNdc(Box({ -1, -1, -11 }, { 1, 1, -9 }), ViewProjection());
    EXPECT_FALSE(bounds.IsEmpty());
    EXPECT_FALSE(bounds.WholeView);
    EXPECT_LT(bounds.Min[0], 0.0f);
    EXPECT_GT(bounds.Max[0], 0.0f);
}

TEST(ScreenBounds, ABoxWhollyBehindTheEyeCoversNothing)
{
    EXPECT_TRUE(ProjectToNdc(Box({ -1, -1, 5 }, { 1, 1, 7 }), ViewProjection()).IsEmpty());
}

TEST(ScreenBounds, ABoxStraddlingTheEyeTakesTheWholeView)
{
    const NdcBounds bounds = ProjectToNdc(Box({ -1, -1, -5 }, { 1, 1, 5 }), ViewProjection());
    EXPECT_TRUE(bounds.WholeView);
    EXPECT_FLOAT_EQ(bounds.Min[0], -1.0f);
    EXPECT_FLOAT_EQ(bounds.Max[1], 1.0f);
}

TEST(ScreenBounds, AWholeViewWinsAUnionAndAnEmptyAddsNothing)
{
    const NdcBounds front = ProjectToNdc(Box({ -1, -1, -11 }, { 1, 1, -9 }), ViewProjection());
    NdcBounds unioned;
    unioned.Include(front);
    unioned.Include(ProjectToNdc(Box({ -1, -1, 5 }, { 1, 1, 7 }), ViewProjection()));
    EXPECT_FLOAT_EQ(unioned.Min[0], front.Min[0]);
    EXPECT_FLOAT_EQ(unioned.Max[1], front.Max[1]);

    unioned.Include(ProjectToNdc(Box({ -1, -1, -5 }, { 1, 1, 5 }), ViewProjection()));
    EXPECT_TRUE(unioned.WholeView);
    EXPECT_FLOAT_EQ(unioned.Min[0], -1.0f);
    unioned.Include(front);
    EXPECT_TRUE(unioned.WholeView) << "a later box must not shrink a whole view";
}
