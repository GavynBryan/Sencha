// Placement for buttons that float over a viewport. The panel draws them and
// the tool hit-tests them, so the rule they share is worth pinning on its own.
#include "viewport/ViewportButtonMath.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{
const ImVec2 kRegionMin{ 0.0f, 0.0f };
const ImVec2 kRegionMax{ 800.0f, 600.0f };

std::vector<std::optional<ImVec2>> Box(float x0, float y0, float x1, float y1)
{
    return { ImVec2(x0, y0), ImVec2(x1, y0), ImVec2(x1, y1), ImVec2(x0, y1) };
}
}

TEST(ViewportButtons, TheRowSitsAboveTheShapeAndCentredOnIt)
{
    const ViewportButtons::Row row =
        ViewportButtons::Layout(Box(300, 250, 500, 400), 2, 1.0f, kRegionMin, kRegionMax);
    ASSERT_TRUE(row.Visible);
    EXPECT_EQ(row.Count, 2);

    const float width = 2.0f * ViewportButtons::kSize + ViewportButtons::kGap;
    EXPECT_FLOAT_EQ(row.Min.x + width * 0.5f, 400.0f) << "not centred on the shape";
    EXPECT_FLOAT_EQ(row.MaxOf(1).y, 250.0f - ViewportButtons::kGap) << "not resting above it";
}

TEST(ViewportButtons, EverySizeFollowsTheUiScale)
{
    const ViewportButtons::Row normal =
        ViewportButtons::Layout(Box(300, 250, 500, 400), 2, 1.0f, kRegionMin, kRegionMax);
    const ViewportButtons::Row scaled =
        ViewportButtons::Layout(Box(300, 250, 500, 400), 2, 2.0f, kRegionMin, kRegionMax);
    EXPECT_FLOAT_EQ(scaled.Size, normal.Size * 2.0f);
    EXPECT_FLOAT_EQ(scaled.Gap, normal.Gap * 2.0f);
    // Still centred, and lifted twice as far clear of the shape.
    const float width = 2.0f * scaled.Size + scaled.Gap;
    EXPECT_FLOAT_EQ(scaled.Min.x + width * 0.5f, 400.0f);
    EXPECT_FLOAT_EQ(scaled.MaxOf(1).y, 250.0f - scaled.Gap);
}

TEST(ViewportButtons, AShapeAgainstTheEdgeKeepsTheRowInTheRegion)
{
    const ViewportButtons::Row top =
        ViewportButtons::Layout(Box(10, 2, 60, 40), 2, 1.0f, kRegionMin, kRegionMax);
    ASSERT_TRUE(top.Visible);
    EXPECT_GE(top.Min.y, kRegionMin.y + ViewportButtons::kMargin);
    EXPECT_GE(top.Min.x, kRegionMin.x + ViewportButtons::kMargin);

    const ViewportButtons::Row right =
        ViewportButtons::Layout(Box(770, 300, 795, 340), 2, 1.0f, kRegionMin, kRegionMax);
    ASSERT_TRUE(right.Visible);
    EXPECT_LE(right.MaxOf(1).x, kRegionMax.x - ViewportButtons::kMargin);
}

TEST(ViewportButtons, AnAnchorBehindTheCameraHidesTheRowRatherThanPinningIt)
{
    // Clamping a projection that means nothing would leave a live button
    // somewhere the geometry is not.
    const std::vector<std::optional<ImVec2>> gone(4, std::nullopt);
    const ViewportButtons::Row row =
        ViewportButtons::Layout(gone, 2, 1.0f, kRegionMin, kRegionMax);
    EXPECT_FALSE(row.Visible);
    EXPECT_EQ(row.HitTest(ImVec2(10.0f, 10.0f)), -1);
}

TEST(ViewportButtons, PartlyBehindTheCameraStillPlacesFromWhatIsVisible)
{
    std::vector<std::optional<ImVec2>> partial = Box(300, 250, 500, 400);
    partial[2] = std::nullopt;
    partial[3] = std::nullopt;
    const ViewportButtons::Row row =
        ViewportButtons::Layout(partial, 2, 1.0f, kRegionMin, kRegionMax);
    ASSERT_TRUE(row.Visible);
    const float width = 2.0f * ViewportButtons::kSize + ViewportButtons::kGap;
    EXPECT_FLOAT_EQ(row.Min.x + width * 0.5f, 400.0f);
}

TEST(ViewportButtons, HitTestingNamesTheButtonUnderThePoint)
{
    const ViewportButtons::Row row =
        ViewportButtons::Layout(Box(300, 250, 500, 400), 3, 1.0f, kRegionMin, kRegionMax);
    ASSERT_TRUE(row.Visible);
    for (int i = 0; i < 3; ++i)
    {
        const ImVec2 min = row.MinOf(i);
        const ImVec2 max = row.MaxOf(i);
        EXPECT_EQ(row.HitTest(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f)), i);
    }
    // The gap between two buttons belongs to neither.
    EXPECT_EQ(row.HitTest(ImVec2(row.MaxOf(0).x + row.Gap * 0.5f, row.Min.y + row.Size * 0.5f)), -1);
    EXPECT_EQ(row.HitTest(ImVec2(row.Min.x - 5.0f, row.Min.y + row.Size * 0.5f)), -1);
}

TEST(ViewportButtons, NoButtonsMeansNoRow)
{
    EXPECT_FALSE(ViewportButtons::Layout(Box(300, 250, 500, 400), 0, 1.0f, kRegionMin, kRegionMax)
                     .Visible);
}

TEST(ViewportButtons, ACaptionIsPlacedByTheSameRuleAsTheButtonsItDescribes)
{
    const ViewportButtons::Row row =
        ViewportButtons::Layout(Box(300, 250, 500, 400), 4, 1.0f, kRegionMin, kRegionMax);
    ASSERT_TRUE(row.Visible);

    // Centred over the span of the buttons it labels, and clear of the row.
    const ImVec2 overFirstPair = row.CaptionCenter(0, 1);
    EXPECT_FLOAT_EQ(overFirstPair.x, (row.MinOf(0).x + row.MaxOf(1).x) * 0.5f);
    EXPECT_LT(overFirstPair.y, row.Min.y) << "the caption should sit above the row";

    // A different span moves it.
    EXPECT_GT(row.CaptionCenter(2, 3).x, overFirstPair.x);

    // Out-of-range indices are pinned rather than read off the end.
    EXPECT_FLOAT_EQ(row.CaptionCenter(-5, 99).x, (row.MinOf(0).x + row.MaxOf(3).x) * 0.5f);
}

TEST(ViewportButtons, ARowWithNoButtonsHasNowhereToPutACaption)
{
    const ViewportButtons::Row empty =
        ViewportButtons::Layout(Box(300, 250, 500, 400), 0, 1.0f, kRegionMin, kRegionMax);
    ASSERT_FALSE(empty.Visible);
    EXPECT_FLOAT_EQ(empty.CaptionCenter(0, 1).x, empty.Min.x);
}
