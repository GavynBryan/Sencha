// A press on a stage: a click until it moves a few pixels, then turntable
// orbit for as long as it is held.

#include "presenters/StageOrbit.h"

#include <gtest/gtest.h>

TEST(StageOrbit, APressIsAClickUntilItMovesThenItOrbits)
{
    StageOrbitDrag drag;
    EXPECT_FALSE(drag.Move({ 10.0f, 10.0f }, { 5.0f, 0.0f }).has_value()) << "nothing turns before a press";

    drag.Press({ 100.0f, 100.0f });
    EXPECT_FALSE(drag.Move({ 101.0f, 101.0f }, { 1.0f, 1.0f }).has_value()) << "a jitter is still a click";
    EXPECT_TRUE(drag.Release());

    drag.Press({ 100.0f, 100.0f });
    const std::optional<StageOrbitDrag::Turn> turn = drag.Move({ 120.0f, 90.0f }, { 20.0f, -10.0f });
    ASSERT_TRUE(turn.has_value());
    EXPECT_GT(turn->Yaw, 0.0f) << "a drag right orbits right";
    EXPECT_GT(turn->Pitch, 0.0f) << "a drag up orbits above";
    EXPECT_TRUE(drag.Move({ 120.0f, 90.0f }, { 0.5f, 0.0f }).has_value()) << "once orbiting, every move turns";
    EXPECT_FALSE(drag.Release()) << "an orbit is not a click";
    EXPECT_FALSE(drag.IsHeld());

    drag.Press({ 0.0f, 0.0f });
    drag.Cancel();
    EXPECT_FALSE(drag.IsHeld());
}

TEST(StageOrbit, ATinyStageShowsNoPicture)
{
    EXPECT_FALSE(CanShowStage(Rect2d(0.0f, 0.0f, 4.0f, 400.0f)));
    EXPECT_TRUE(CanShowStage(Rect2d(0.0f, 0.0f, 8.0f, 8.0f)));
}
