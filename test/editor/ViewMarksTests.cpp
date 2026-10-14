#include "overlay/EditorOverlayState.h"
#include "overlay/ViewMarks.h"
#include "viewport/EditorViewport.h"
#include "viewport/MarqueeState.h"
#include "viewport/ViewportProjection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace
{
EditorViewport TopView(ViewportId id, Vec2d min)
{
    EditorViewport viewport;
    viewport.ApplyOrientation(ViewportOrientation::Top);
    viewport.Id = id;
    viewport.RegionMin = min;
    viewport.RegionMax = min + Vec2d(400.0f, 400.0f);
    return viewport;
}

std::vector<ViewMark> MarksOf(const EditorOverlayState& overlay, const MarqueeState& marquee,
                              const EditorViewport& viewport)
{
    std::vector<ViewMark> marks;
    AppendViewMarks(overlay, marquee, viewport, 1.0f, marks);
    return marks;
}

std::size_t CountOf(const std::vector<ViewMark>& marks, ViewMarkKind kind)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(marks, [&](const ViewMark& mark) { return mark.Kind == kind; }));
}
} // namespace

TEST(ViewMarks, ALabelSitsBesideThePointItNamesInEachView)
{
    const EditorViewport left = TopView(ViewportId{ 1 }, Vec2d(0.0f, 0.0f));
    const EditorViewport right = TopView(ViewportId{ 2 }, Vec2d(500.0f, 0.0f));
    EditorOverlayState overlay;
    overlay.Labels.push_back({ .World = Vec3d(0.0f, 0.0f, 0.0f), .Color = Vec4(1, 1, 1, 1), .Text = "16", .Axis = 0 });
    const MarqueeState marquee;

    for (const EditorViewport* viewport : { &left, &right })
    {
        const std::vector<ViewMark> marks = MarksOf(overlay, marquee, *viewport);
        ASSERT_EQ(marks.size(), 1u);
        const Vec2d at = ViewportProjection(*viewport).WorldToPixel(Vec3d{})->Pixel;
        EXPECT_EQ(marks[0].Text, "16");
        EXPECT_GT(marks[0].Rect.Position.X, at.X) << "beside the point, in that view's own pixels";
        EXPECT_LT(marks[0].Rect.Position.X, at.X + 10.0f);
    }
}

TEST(ViewMarks, AnOrthoViewHidesTheDimensionItLooksAlong)
{
    const EditorViewport top = TopView(ViewportId{ 1 }, Vec2d{});
    EditorOverlayState overlay;
    overlay.Labels.push_back({ .World = Vec3d{}, .Color = Vec4(1, 1, 1, 1), .Text = "height", .Axis = 1 });
    overlay.Labels.push_back({ .World = Vec3d{}, .Color = Vec4(1, 1, 1, 1), .Text = "width", .Axis = 0 });
    const std::vector<ViewMark> marks = MarksOf(overlay, MarqueeState{}, top);
    ASSERT_EQ(marks.size(), 1u) << "a top view cannot show a height";
    EXPECT_EQ(marks[0].Text, "width");
}

TEST(ViewMarks, WhatNamesAViewAppearsOnlyThere)
{
    const EditorViewport one = TopView(ViewportId{ 1 }, Vec2d{});
    const EditorViewport two = TopView(ViewportId{ 2 }, Vec2d(500.0f, 0.0f));
    EditorOverlayState overlay;
    overlay.PointHandles.push_back({ .World = Vec3d{}, .Fill = Vec4(1, 1, 1, 1), .Border = Vec4(0, 0, 0, 1),
                                     .Viewport = ViewportId{ 2 } });
    MarqueeState marquee;
    marquee.Active = true;
    marquee.Viewport = ViewportId{ 1 };
    marquee.Start = Vec2d(10.0f, 10.0f);
    marquee.Current = Vec2d(5.0f, 40.0f);

    const std::vector<ViewMark> onOne = MarksOf(overlay, marquee, one);
    const std::vector<ViewMark> onTwo = MarksOf(overlay, marquee, two);
    EXPECT_EQ(CountOf(onOne, ViewMarkKind::Handle), 0u);
    EXPECT_EQ(CountOf(onTwo, ViewMarkKind::Handle), 1u);
    ASSERT_EQ(CountOf(onOne, ViewMarkKind::Marquee), 1u);
    EXPECT_EQ(CountOf(onTwo, ViewMarkKind::Marquee), 0u);
    const ViewMark& band = *std::ranges::find_if(onOne, [](const ViewMark& m) { return m.Kind == ViewMarkKind::Marquee; });
    EXPECT_EQ(band.Rect, Rect2d(5.0f, 10.0f, 5.0f, 30.0f)) << "a band dragged up and left is the same box";
}

TEST(ViewMarks, AHotButtonSaysWhatItDoes)
{
    const EditorViewport top = TopView(ViewportId{ 1 }, Vec2d{});
    EditorOverlayState overlay;
    ViewportButtonRequest row;
    row.Anchors = { Vec3d(-1.0f, 0.0f, -1.0f), Vec3d(1.0f, 0.0f, 1.0f) };
    ViewportButton apply;
    apply.Icon = IconId::Check;
    apply.Tooltip = "Apply";
    ViewportButton cancel;
    cancel.Icon = IconId::Cancel;
    cancel.Tooltip = "Cancel";
    row.Buttons = { apply, cancel };
    row.Hot = 1;
    overlay.ViewportButtons.push_back(row);

    const std::vector<ViewMark> marks = MarksOf(overlay, MarqueeState{}, top);
    ASSERT_EQ(CountOf(marks, ViewMarkKind::Button), 2u);
    ASSERT_EQ(CountOf(marks, ViewMarkKind::Tooltip), 1u);
    const ViewMark& tip = *std::ranges::find_if(marks, [](const ViewMark& m) { return m.Kind == ViewMarkKind::Tooltip; });
    EXPECT_EQ(tip.Text, "Cancel");
    const auto hot = std::ranges::find_if(marks, [](const ViewMark& m) { return m.Kind == ViewMarkKind::Button && m.Hot; });
    ASSERT_NE(hot, marks.end());
    EXPECT_EQ(hot->Icon, IconId::Cancel);
}
