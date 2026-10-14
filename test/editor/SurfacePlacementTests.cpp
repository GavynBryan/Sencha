#include "shell/SurfacePlacement.h"

#include <gtest/gtest.h>

#include <vector>

// A floating surface of the shell opens where there is room, stays where it
// opened, and changes place or size only when the person moves or sizes it.
namespace
{
constexpr float kStep = 16.0f;
const Rect2d kBounds(8.0f, 44.0f, 1264.0f, 668.0f);
const Rect2d kStation(8.0f, 560.0f, 260.0f, 152.0f);
} // namespace

TEST(SurfacePlacement, ASurfaceOpensAtHomeWhenNothingIsThere)
{
    const Rect2d home(8.0f, 44.0f, 340.0f, 500.0f);
    EXPECT_EQ(FindSpace(home, std::vector<Rect2d>{ kStation }, kBounds, kStep), home);
}

TEST(SurfacePlacement, ASurfaceOpensInTheFreeSpaceNearestHome)
{
    const Rect2d card(972.0f, 300.0f, 300.0f, 360.0f);
    const Rect2d home(8.0f, 300.0f, 640.0f, 300.0f);
    const Rect2d placed = FindSpace(home, std::vector<Rect2d>{ kStation, card }, kBounds, kStep);
    EXPECT_EQ(placed.Size, home.Size) << "nothing is resized to make room";
    for (const Rect2d& taken : { kStation, card })
        EXPECT_FALSE(placed.Min().X < taken.Max().X && taken.Min().X < placed.Max().X && placed.Min().Y < taken.Max().Y
                     && taken.Min().Y < placed.Max().Y);
    EXPECT_LT((placed.Position - home.Position).Magnitude(), 300.0f) << "the nearest room, not any room";
}

TEST(SurfacePlacement, WithNoRoomASurfaceOpensAtHomeOverWhatIsThere)
{
    const Rect2d everything = kBounds;
    const Rect2d home(8.0f, 300.0f, 640.0f, 300.0f);
    EXPECT_EQ(FindSpace(home, std::vector<Rect2d>{ everything }, kBounds, kStep), home);
}

TEST(SurfacePlacement, AnOpenSurfaceStaysWhereItOpenedUntilItCloses)
{
    SurfaceLayout layout;
    const Rect2d home(8.0f, 300.0f, 400.0f, 260.0f);
    const Rect2d first = layout.Place("explorer.pictures", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep);
    EXPECT_EQ(first, home);

    // Something opening over home later does not push it around.
    const Rect2d again = layout.Place("explorer.pictures", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{ home }, kBounds, kStep);
    EXPECT_EQ(again, first);

    // Closed and opened again, it looks for room afresh.
    layout.Hide("explorer.pictures");
    const Rect2d reopened = layout.Place("explorer.pictures", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{ home }, kBounds, kStep);
    EXPECT_NE(reopened.Position, home.Position);
}

TEST(SurfacePlacement, AHeaderMovesASurfaceByThePointersWholeTravelAndItStaysThere)
{
    SurfaceLayout layout;
    const Rect2d home(8.0f, 300.0f, 400.0f, 260.0f);
    (void)layout.Place("explorer.pictures", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep);

    layout.PressHeader("explorer.pictures", Vec2d(100.0f, 310.0f));
    EXPECT_EQ(layout.State(), SurfaceLayout::Phase::PressedHeader);
    layout.Drag(Vec2d(110.0f, 300.0f), kBounds, Vec2d(220.0f, 140.0f));
    layout.Drag(Vec2d(300.0f, 210.0f), kBounds, Vec2d(220.0f, 140.0f));
    layout.Drop();

    const Rect2d moved = layout.Place("explorer.pictures", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep);
    EXPECT_EQ(moved, Rect2d(208.0f, 200.0f, 400.0f, 260.0f));

    // A moved surface comes back where it was left.
    layout.Hide("explorer.pictures");
    EXPECT_EQ(layout.Place("explorer.pictures", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep), moved);
}

TEST(SurfacePlacement, ACornerSizesASurfaceWithinTheWindowAndItsMinimum)
{
    SurfaceLayout layout;
    const Rect2d home(8.0f, 300.0f, 400.0f, 260.0f);
    (void)layout.Place("explorer.outline", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep);
    EXPECT_FALSE(layout.Sized("explorer.outline"));

    layout.PressCorner("explorer.outline", Vec2d(408.0f, 560.0f));
    layout.Drag(Vec2d(108.0f, 900.0f), kBounds, Vec2d(220.0f, 140.0f));
    layout.Drop();
    const Rect2d sized = layout.Place("explorer.outline", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep);
    EXPECT_FLOAT_EQ(sized.Width(), 220.0f) << "no smaller than the minimum";
    EXPECT_FLOAT_EQ(sized.Max().Y, kBounds.Max().Y) << "no further than the window";
    EXPECT_TRUE(layout.Sized("explorer.outline"));

    layout.Reset("explorer.outline");
    EXPECT_FALSE(layout.Sized("explorer.outline"));
    EXPECT_EQ(layout.Place("explorer.outline", SurfaceAnchor::FreeSpace, home, std::vector<Rect2d>{}, kBounds, kStep), home);
}

TEST(SurfacePlacement, APressThatNeverMovesChangesNothing)
{
    SurfaceLayout layout;
    const Rect2d home(8.0f, 300.0f, 400.0f, 260.0f);
    (void)layout.Place("selection.card", SurfaceAnchor::Home, home, std::vector<Rect2d>{}, kBounds, kStep);
    layout.PressHeader("selection.card", Vec2d(20.0f, 310.0f));
    layout.Drop();
    layout.Hide("selection.card");
    EXPECT_EQ(layout.Place("selection.card", SurfaceAnchor::Home, home, std::vector<Rect2d>{ home }, kBounds, kStep).Size, home.Size);
    layout.Drag(Vec2d(500.0f, 500.0f), kBounds, Vec2d(220.0f, 140.0f));
    EXPECT_EQ(layout.State(), SurfaceLayout::Phase::Resting);
}

TEST(SurfacePlacement, AMovedSurfaceStaysInsideTheWindow)
{
    const Rect2d card(970.0f, 340.0f, 300.0f, 330.0f);
    EXPECT_EQ(KeepInside(Vec2d(500.0f, 500.0f), card, kBounds), Vec2d(2.0f, 42.0f));
    const Rect2d taller(100.0f, 44.0f, 200.0f, 900.0f);
    EXPECT_EQ(KeepInside(Vec2d(0.0f, 200.0f), taller, kBounds), Vec2d(0.0f, 0.0f));
}

TEST(SurfacePlacement, AnAnchoredSurfaceKeepsToItsHomeUntilItIsMoved)
{
    SurfaceLayout layout;
    const Rect2d shortCard(972.0f, 500.0f, 300.0f, 160.0f);
    const Rect2d tallCard(972.0f, 360.0f, 300.0f, 300.0f);
    EXPECT_EQ(layout.Place("selection.card", SurfaceAnchor::Home, shortCard, std::vector<Rect2d>{}, kBounds, kStep),
              shortCard);
    EXPECT_EQ(layout.Place("selection.card", SurfaceAnchor::Home, tallCard, std::vector<Rect2d>{}, kBounds, kStep),
              tallCard) << "its bottom stays put as its content grows";

    layout.PressHeader("selection.card", Vec2d(1000.0f, 370.0f));
    layout.Drag(Vec2d(800.0f, 370.0f), kBounds, Vec2d(220.0f, 140.0f));
    layout.Drop();
    EXPECT_EQ(layout.Place("selection.card", SurfaceAnchor::Home, shortCard, std::vector<Rect2d>{}, kBounds, kStep).Position,
              Vec2d(772.0f, 360.0f)) << "moved, it stays where the person put it";
}

TEST(SurfacePlacement, SomethingOpenedBesideAnAnchorGoesTowardTheWindowsRoom)
{
    const Vec2d window(1000.0f, 800.0f);
    const OpeningToward topLeft = OpenTowardRoom(Rect2d(20.0f, 20.0f, 60.0f, 20.0f), window);
    EXPECT_FALSE(topLeft.Left);
    EXPECT_FALSE(topLeft.Up);
    const OpeningToward bottomRight = OpenTowardRoom(Rect2d(900.0f, 760.0f, 60.0f, 20.0f), window);
    EXPECT_TRUE(bottomRight.Left);
    EXPECT_TRUE(bottomRight.Up);
}
