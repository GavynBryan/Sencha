// A horizontal cut through a placed brush: where the plane meets the solid, as
// outlines on the ground plane (x = world X, y = world Z).

#include "brush/BrushOps.h"
#include "brush/BrushSection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{
    Transform3f At(const Vec3d& position)
    {
        Transform3f transform;
        transform.Position = position;
        return transform;
    }

    double Area(const BrushSectionContour& contour)
    {
        double twice = 0.0;
        const std::size_t count = contour.Points.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vec2d& a = contour.Points[i];
            const Vec2d& b = contour.Points[(i + 1) % count];
            twice += static_cast<double>(a.X) * b.Y - static_cast<double>(b.X) * a.Y;
        }
        return std::abs(twice) / 2.0;
    }
}

TEST(BrushSection, ABoxCutThroughItsMiddleIsOneRectangleWherePlaced)
{
    const BrushMesh box = BrushOps::MakeBox({ 2.0f, 1.0f, 3.0f });
    const std::vector<BrushSectionContour> contours =
        SectionBrushAtHeight(box, At({ 10.0f, 0.0f, -5.0f }), 0.25f);
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_TRUE(contours.front().Closed);
    EXPECT_NEAR(Area(contours.front()), 4.0 * 6.0, 1e-4);
    for (const Vec2d& point : contours.front().Points)
    {
        EXPECT_NEAR(std::abs(point.X - 10.0f), 2.0f, 1e-4f);
        EXPECT_NEAR(std::abs(point.Y + 5.0f), 3.0f, 1e-4f);
    }
}

TEST(BrushSection, APlaneThatMissesTheBrushCutsNothing)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    EXPECT_TRUE(SectionBrushAtHeight(box, At({}), 5.0f).empty());
    EXPECT_TRUE(SectionBrushAtHeight(box, At({}), -5.0f).empty());
}

TEST(BrushSection, AnOpenSurfaceLeavesAnOpenRun)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    // Remove one side wall: the cut through the remaining walls cannot close.
    const auto side = std::ranges::find_if(box.Faces, [](const BrushFace& face) {
        return face.Normal.X > 0.9f;
    });
    ASSERT_NE(side, box.Faces.end());
    box = BrushOps::DeleteFace(box, static_cast<std::uint32_t>(side - box.Faces.begin()));

    const std::vector<BrushSectionContour> contours = SectionBrushAtHeight(box, At({}), 0.0f);
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_FALSE(contours.front().Closed);
    EXPECT_EQ(contours.front().Points.size(), 4u);
}

TEST(BrushSection, TheSameBrushAlwaysAnswersTheSameWay)
{
    const BrushMesh box = BrushOps::MakeBox({ 2.0f, 1.0f, 3.0f });
    const auto first = SectionBrushAtHeight(box, At({ 1.0f, 0.0f, 1.0f }), 0.1f);
    const auto second = SectionBrushAtHeight(box, At({ 1.0f, 0.0f, 1.0f }), 0.1f);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
        for (std::size_t p = 0; p < first[i].Points.size(); ++p)
        {
            EXPECT_EQ(first[i].Points[p].X, second[i].Points[p].X);
            EXPECT_EQ(first[i].Points[p].Y, second[i].Points[p].Y);
        }
}
