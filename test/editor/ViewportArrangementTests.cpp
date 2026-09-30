#include "viewport/ViewportArrangement.h"
#include "input/InputRouter.h"
#include "viewport/ViewDividerDrag.h"
#include "viewport/ViewportLayout.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
const Rect2d kContent(0.0f, 34.0f, 1280.0f, 686.0f);
}

TEST(ViewportArrangement, OneViewFillsTheContent)
{
    const std::vector<Rect2d> tiles = ArrangeViewports(ViewportArrangementKind::Single, kContent, { 0.5f, 0.5f }, 4.0f);
    ASSERT_EQ(tiles.size(), TileCount(ViewportArrangementKind::Single));
    EXPECT_EQ(tiles[0], kContent) << "no gutter around a lone view";
}

TEST(ViewportArrangement, TilesMeetAtTheDividerWithAGutterBetween)
{
    struct Case
    {
        ViewportArrangementKind Kind;
        Vec2d Divider;
        std::vector<Rect2d> Expected;
    };
    const Case cases[] = {
        { ViewportArrangementKind::Split, { 0.5f, 0.5f },
          { Rect2d(0, 34, 638, 686), Rect2d(642, 34, 638, 686) } },
        { ViewportArrangementKind::Split, { 0.25f, 0.5f },
          { Rect2d(0, 34, 318, 686), Rect2d(322, 34, 958, 686) } },
        { ViewportArrangementKind::Quad, { 0.5f, 0.5f },
          { Rect2d(0, 34, 638, 341), Rect2d(642, 34, 638, 341), Rect2d(0, 379, 638, 341),
            Rect2d(642, 379, 638, 341) } },
    };
    for (const Case& c : cases)
    {
        const std::vector<Rect2d> tiles = ArrangeViewports(c.Kind, kContent, c.Divider, 4.0f);
        ASSERT_EQ(tiles.size(), TileCount(c.Kind));
        for (std::size_t i = 0; i < tiles.size(); ++i)
            EXPECT_EQ(tiles[i], c.Expected[i]) << "tile " << i;
    }
}

TEST(ViewportArrangement, ADividerCannotSqueezeATileAway)
{
    const std::vector<Rect2d> tiles = ArrangeViewports(ViewportArrangementKind::Split, kContent, { 0.0f, 0.5f }, 4.0f);
    EXPECT_GE(tiles[0].Width(), kContent.Width() * 0.15f - 3.0f);
    const std::vector<Rect2d> far = ArrangeViewports(ViewportArrangementKind::Split, kContent, { 1.0f, 0.5f }, 4.0f);
    EXPECT_GE(far[1].Width(), kContent.Width() * 0.15f - 3.0f);
}

TEST(ViewportArrangement, TilesLandOnWholePixels)
{
    const Rect2d odd(0.5f, 33.7f, 1001.3f, 500.9f);
    for (const Rect2d& tile : ArrangeViewports(ViewportArrangementKind::Quad, odd, { 0.37f, 0.61f }, 3.0f))
    {
        EXPECT_EQ(tile.Position.X, std::round(tile.Position.X));
        EXPECT_EQ(tile.Position.Y, std::round(tile.Position.Y));
        EXPECT_EQ(tile.Size.X, std::round(tile.Size.X));
        EXPECT_EQ(tile.Size.Y, std::round(tile.Size.Y));
    }
}

namespace
{
ViewportId IdOf(const ViewportLayout& layout, ViewportOrientation orientation)
{
    for (const ViewportLayout::ViewportStorage& viewport : layout.All())
        if (viewport->Orientation == orientation)
            return viewport->Id;
    return {};
}
} // namespace

TEST(ViewportArrangement, EachArrangementShowsTheViewsItNames)
{
    ViewportLayout layout = ViewportLayout::MakeDefault();
    const ViewportId perspective = IdOf(layout, ViewportOrientation::Perspective);
    const ViewportId top = IdOf(layout, ViewportOrientation::Top);
    const ViewportId front = IdOf(layout, ViewportOrientation::Front);
    const ViewportId side = IdOf(layout, ViewportOrientation::Right);

    EXPECT_EQ(ViewsShownIn(ViewportArrangementKind::Single, layout), std::vector<ViewportId>{ perspective });
    EXPECT_EQ(ViewsShownIn(ViewportArrangementKind::Split, layout), (std::vector<ViewportId>{ perspective, top }))
        << "with perspective active, the first ortho view keeps it company";
    EXPECT_EQ(ViewsShownIn(ViewportArrangementKind::Quad, layout),
              (std::vector<ViewportId>{ perspective, top, front, side }));

    layout.SetActive(front);
    EXPECT_EQ(ViewsShownIn(ViewportArrangementKind::Single, layout), std::vector<ViewportId>{ front })
        << "one view is whichever is active: swapping projection is choosing another view";
    EXPECT_EQ(ViewsShownIn(ViewportArrangementKind::Split, layout), (std::vector<ViewportId>{ perspective, front }));
}

TEST(ViewportArrangement, APressNearADividerGripsIt)
{
    struct Case
    {
        ViewportArrangementKind Kind;
        Vec2d Point;
        bool X;
        bool Y;
    };
    const Case cases[] = {
        { ViewportArrangementKind::Single, { 640.0f, 377.0f }, false, false },
        { ViewportArrangementKind::Split, { 643.0f, 100.0f }, true, false },
        { ViewportArrangementKind::Split, { 660.0f, 100.0f }, false, false },
        { ViewportArrangementKind::Split, { 640.0f, 377.0f }, true, false },
        { ViewportArrangementKind::Quad, { 640.0f, 377.0f }, true, true },
        { ViewportArrangementKind::Quad, { 200.0f, 379.0f }, false, true },
        { ViewportArrangementKind::Quad, { 640.0f, 10.0f }, false, false },
    };
    for (const Case& c : cases)
    {
        const DividerGrip grip = GripDivider(c.Kind, kContent, { 0.5f, 0.5f }, c.Point, 5.0f);
        EXPECT_EQ(grip.X, c.X) << c.Point.X << ", " << c.Point.Y;
        EXPECT_EQ(grip.Y, c.Y) << c.Point.X << ", " << c.Point.Y;
    }
}

TEST(ViewportArrangement, AHeldDividerFollowsThePointerAndStaysClearOfTheEdges)
{
    const Vec2d moved = MoveDivider(kContent, { 0.5f, 0.5f }, { .X = true, .Y = false }, { 320.0f, 600.0f });
    EXPECT_FLOAT_EQ(moved.X, 0.25f);
    EXPECT_FLOAT_EQ(moved.Y, 0.5f) << "the axis not held stays";
    const Vec2d pinned = MoveDivider(kContent, { 0.5f, 0.5f }, { .X = true, .Y = true }, { 0.0f, 2000.0f });
    EXPECT_GT(pinned.X, 0.0f);
    EXPECT_LT(pinned.Y, 1.0f);
}

TEST(ViewDividerDrag, DraggingTheDividerTakesThePointerAndLetsGoOnRelease)
{
    Vec2d divider{ 0.5f, 0.5f };
    ViewDividerDrag drag(
        [&] { return ViewDividerDrag::Arrangement{ .Kind = ViewportArrangementKind::Split, .Content = kContent, .Divider = divider }; },
        [&](Vec2d moved) { divider = moved; });
    bool after = false;
    InputRouter router;
    router.AddHandler([&](const InputEvent& e, PointerCapture& cap) { return drag.OnInput(e, cap); });
    router.AddHandler([&](const InputEvent&, PointerCapture&) { after = true; return InputConsumed::No; });

    EXPECT_EQ(router.Route(PointerDownEvent{ .Position = { 641.0f, 300.0f }, .Button = MouseButton::Left, .Modifiers = {} }),
              InputConsumed::Yes);
    (void)router.Route(PointerMoveEvent{ .Position = { 960.0f, 300.0f }, .Delta = {}, .Modifiers = {} });
    EXPECT_FLOAT_EQ(divider.X, 0.75f);
    EXPECT_FALSE(after) << "the drag holds the pointer";
    (void)router.Route(PointerUpEvent{ .Position = { 960.0f, 300.0f }, .Button = MouseButton::Left, .Modifiers = {} });

    after = false;
    (void)router.Route(PointerMoveEvent{ .Position = { 100.0f, 300.0f }, .Delta = {}, .Modifiers = {} });
    EXPECT_FLOAT_EQ(divider.X, 0.75f) << "released, the divider stays where it was left";
    EXPECT_TRUE(after);

    EXPECT_EQ(router.Route(PointerDownEvent{ .Position = { 100.0f, 300.0f }, .Button = MouseButton::Left, .Modifiers = {} }),
              InputConsumed::No) << "a press away from the divider is the views'";
}
