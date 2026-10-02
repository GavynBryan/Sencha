#include "shell/SurfacePlacement.h"

#include <gtest/gtest.h>

#include <vector>

// A floating surface of the shell makes way for the instruments it would cover
// in its default place, and once the person moves it, sits where they put it,
// inside the window.
namespace
{
constexpr float kGap = 8.0f;
const Rect2d kWindow(0.0f, 34.0f, 1280.0f, 686.0f);
} // namespace

TEST(SurfacePlacement, ATrayGivesWayToTheStationOnItsLeftAndTheCardOnItsRight)
{
    const Rect2d tray(8.0f, 398.0f, 1264.0f, 272.0f);
    const std::vector<SurfaceObstacle> obstacles{
        { Rect2d(8.0f, 580.0f, 260.0f, 132.0f), SurfaceYield::ShrinkLeft },
        { Rect2d(970.0f, 340.0f, 302.0f, 330.0f), SurfaceYield::ShrinkRight },
    };
    const SurfaceClearance clearance = ClearOf(tray, obstacles, kGap);
    EXPECT_FLOAT_EQ(clearance.Left, 268.0f + kGap - 8.0f);
    EXPECT_FLOAT_EQ(clearance.Right, 1272.0f - (970.0f - kGap));
    EXPECT_FLOAT_EQ(clearance.Bottom, 0.0f);
}

TEST(SurfacePlacement, NothingInTheWayAndNothingShownAskNothing)
{
    const Rect2d outline(8.0f, 42.0f, 340.0f, 300.0f);
    const std::vector<SurfaceObstacle> obstacles{
        { Rect2d(8.0f, 580.0f, 260.0f, 132.0f), SurfaceYield::ShrinkBottom },
        { Rect2d(), SurfaceYield::ShrinkRight },
    };
    EXPECT_EQ(ClearOf(outline, obstacles, kGap), SurfaceClearance{});
}

TEST(SurfacePlacement, AnOutlineEndsAboveTheStationAndASheetMovesPastIt)
{
    const Rect2d station(8.0f, 500.0f, 260.0f, 212.0f);
    const SurfaceClearance outline =
        ClearOf(Rect2d(8.0f, 42.0f, 340.0f, 628.0f), std::vector<SurfaceObstacle>{ { station, SurfaceYield::ShrinkBottom } },
                kGap);
    EXPECT_FLOAT_EQ(outline.Bottom, 670.0f - (500.0f - kGap));

    const SurfaceClearance sheet = ClearOf(Rect2d(260.0f, 72.0f, 760.0f, 588.0f),
                                           std::vector<SurfaceObstacle>{ { Rect2d(8.0f, 42.0f, 340.0f, 450.0f),
                                                                           SurfaceYield::ShiftRight } },
                                           kGap);
    EXPECT_FLOAT_EQ(sheet.ShiftX, 348.0f + kGap - 260.0f);
}

TEST(SurfacePlacement, GivingWayStopsAtTheSurfacesMinimumAndItOverlapsInstead)
{
    // A narrow window: the station and the card leave the tray 200 wide.
    const Rect2d tray(8.0f, 300.0f, 784.0f, 272.0f);
    const std::vector<SurfaceObstacle> sides{
        { Rect2d(8.0f, 480.0f, 260.0f, 112.0f), SurfaceYield::ShrinkLeft },
        { Rect2d(476.0f, 200.0f, 316.0f, 392.0f), SurfaceYield::ShrinkRight },
    };
    const SurfaceClearance narrowed = ClearOf(tray, sides, kGap, SurfaceMinimum{ .Width = 420.0f });
    EXPECT_FLOAT_EQ(tray.Width() - narrowed.Left - narrowed.Right, 420.0f);
    // Each side gives in proportion to what it asked for.
    EXPECT_FLOAT_EQ(narrowed.Left / narrowed.Right, 268.0f / 324.0f);

    const SurfaceClearance unbounded = ClearOf(tray, sides, kGap);
    EXPECT_FLOAT_EQ(unbounded.Left, 268.0f);

    const Rect2d outline(8.0f, 42.0f, 340.0f, 628.0f);
    const SurfaceClearance tall = ClearOf(outline, std::vector<SurfaceObstacle>{ { Rect2d(8.0f, 100.0f, 260.0f, 612.0f),
                                                                                 SurfaceYield::ShrinkBottom } },
                                          kGap, SurfaceMinimum{ .Height = 200.0f });
    EXPECT_FLOAT_EQ(outline.Height() - tall.Bottom, 200.0f);
}

TEST(SurfacePlacement, TheDefaultPlaceIsWhatWasMeasuredWithItsClearanceGivenBack)
{
    const SurfaceClearance applied{ .Left = 20.0f, .Right = 30.0f, .Bottom = 40.0f, .ShiftX = 50.0f };
    const Rect2d natural = NaturalPlace(Rect2d(100.0f, 100.0f, 200.0f, 100.0f), applied);
    EXPECT_EQ(natural, Rect2d::FromMinMax(Vec2d(80.0f, 100.0f), Vec2d(330.0f, 240.0f)));
}

TEST(SurfacePlacement, AMovedSurfaceStaysInsideTheWindow)
{
    const Rect2d card(970.0f, 340.0f, 300.0f, 330.0f);
    EXPECT_EQ(KeepInside(Vec2d(-100.0f, -50.0f), card, kWindow), Vec2d(-100.0f, -50.0f));
    EXPECT_EQ(KeepInside(Vec2d(500.0f, 500.0f), card, kWindow), Vec2d(10.0f, 50.0f));
    EXPECT_EQ(KeepInside(Vec2d(-2000.0f, -2000.0f), card, kWindow), Vec2d(-970.0f, -306.0f));

    const Rect2d taller(100.0f, 34.0f, 200.0f, 900.0f);
    EXPECT_EQ(KeepInside(Vec2d(0.0f, 200.0f), taller, kWindow), Vec2d(0.0f, 0.0f));
}

TEST(SurfacePlacement, ADragMovesTheSurfaceByThePointersWholeTravelSinceThePress)
{
    SurfaceMoves moves;
    const Rect2d tray(276.0f, 398.0f, 688.0f, 272.0f);
    const SurfaceClearance clearance{ .Left = 268.0f, .Right = 308.0f };
    moves.Press("explorer.pictures", Vec2d(300.0f, 410.0f), clearance);
    EXPECT_EQ(moves.State(), SurfaceMoves::Phase::Pressed);
    EXPECT_EQ(moves.Find("explorer.pictures"), nullptr);

    // The first drag is reported after the pointer has already left the press.
    moves.Drag(Vec2d(290.0f, 400.0f), tray, kWindow);
    moves.Drag(Vec2d(250.0f, 210.0f), tray, kWindow);
    EXPECT_EQ(moves.State(), SurfaceMoves::Phase::Dragging);
    moves.Drop();
    EXPECT_EQ(moves.State(), SurfaceMoves::Phase::Resting);

    const SurfaceMove* moved = moves.Find("explorer.pictures");
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->Offset, Vec2d(-50.0f, -200.0f));
    EXPECT_EQ(moved->Clearance, clearance);
    EXPECT_EQ(moves.Find("explorer.outline"), nullptr);

    // Pressed again, it continues from where it was left, keeping its size.
    moves.Press("explorer.pictures", Vec2d(0.0f, 0.0f), SurfaceClearance{});
    moves.Drag(Vec2d(10.0f, 0.0f), tray, kWindow);
    EXPECT_EQ(moves.Find("explorer.pictures")->Offset, Vec2d(-40.0f, -200.0f));
    EXPECT_EQ(moves.Find("explorer.pictures")->Clearance, clearance);
}

TEST(SurfacePlacement, APressThatNeverDragsMovesNothing)
{
    SurfaceMoves moves;
    moves.Press("selection.card", Vec2d(1000.0f, 350.0f), SurfaceClearance{});
    moves.Drop();
    EXPECT_EQ(moves.Find("selection.card"), nullptr);
}

TEST(SurfacePlacement, ResetPutsASurfaceBackAndADragWithoutAPressDoesNothing)
{
    SurfaceMoves moves;
    moves.Drag(Vec2d(10.0f, 10.0f), Rect2d(0.0f, 100.0f, 10.0f, 10.0f), kWindow);
    EXPECT_EQ(moves.Find("selection.card"), nullptr);

    moves.Press("selection.card", Vec2d(), SurfaceClearance{});
    moves.Drag(Vec2d(-30.0f, -30.0f), Rect2d(970.0f, 340.0f, 300.0f, 330.0f), kWindow);
    moves.Reset("selection.card");
    EXPECT_EQ(moves.Find("selection.card"), nullptr);
    EXPECT_EQ(moves.State(), SurfaceMoves::Phase::Resting);
}
