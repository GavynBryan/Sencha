#include "viewport/ViewportArrangement.h"
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
