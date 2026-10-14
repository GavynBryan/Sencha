#include "shell/ScrubGesture.h"

#include <gtest/gtest.h>

// A number dragged across its box: a press is still a click until the pointer
// has gone a few pixels; after that each pixel moves the number by its scale,
// a tenth as much with Shift, and Ctrl lands it on a whole coarse step.
TEST(ScrubGesture, APressIsAClickUntilThePointerHasGoneAFewPixels)
{
    ScrubGesture gesture(ScrubScale{ .PerPixel = 0.5, .Coarse = 1.0 });
    EXPECT_FALSE(gesture.Move(1.0, false));
    EXPECT_FALSE(gesture.Move(-1.0, false));
    EXPECT_FALSE(gesture.Dragging());
    EXPECT_TRUE(gesture.Move(4.0, false)) << "three pixels across, one way or the other";
    EXPECT_TRUE(gesture.Dragging());
    EXPECT_DOUBLE_EQ(gesture.Amount(0.0, false), 0.0) << "the pixels it took to become a drag move nothing";
}

TEST(ScrubGesture, EachPixelMovesTheNumberByItsScaleAndShiftByATenth)
{
    ScrubGesture gesture(ScrubScale{ .PerPixel = 0.5, .Coarse = 1.0 });
    (void)gesture.Move(5.0, false);
    (void)gesture.Move(10.0, false);
    EXPECT_DOUBLE_EQ(gesture.Amount(0.0, false), 5.0);
    (void)gesture.Move(10.0, true);
    EXPECT_DOUBLE_EQ(gesture.Amount(0.0, false), 5.5) << "Shift held part way changes the rate from there on";
    (void)gesture.Move(-30.0, false);
    EXPECT_DOUBLE_EQ(gesture.Amount(0.0, false), -9.5);
}

TEST(ScrubGesture, CtrlLandsTheNumberOnAWholeCoarseStep)
{
    ScrubGesture gesture(ScrubScale{ .PerPixel = 0.1, .Coarse = 1.0 });
    (void)gesture.Move(5.0, false);
    (void)gesture.Move(13.0, false);
    EXPECT_NEAR(gesture.Amount(0.25, false), 1.3, 1e-9);
    EXPECT_NEAR(0.25 + gesture.Amount(0.25, true), 2.0, 1e-9) << "lands on 2, not 1.55";
}

namespace
{
PropertyDescriptor Described(double min, double max, double step)
{
    PropertyDescriptor descriptor;
    descriptor.Min = min;
    descriptor.Max = max;
    descriptor.Step = step;
    return descriptor;
}
} // namespace

TEST(ScrubGesture, TheScaleFollowsWhatThePropertySaysOfItself)
{
    EXPECT_DOUBLE_EQ(ScrubScaleFor(Described(0.0, -1.0, 1.0)).PerPixel, 1.0);
    EXPECT_DOUBLE_EQ(ScrubScaleFor(Described(0.0, -1.0, 1.0)).Coarse, 5.0);
    EXPECT_DOUBLE_EQ(ScrubScaleFor(Described(0.0, 2.0, 0.0)).PerPixel, 0.01);
    EXPECT_DOUBLE_EQ(ScrubScaleFor(Described(0.0, 2.0, 0.0)).Coarse, 0.1);
    EXPECT_DOUBLE_EQ(ScrubScaleFor(PropertyDescriptor{}).PerPixel, 0.05) << "a number with nothing said of it";
    EXPECT_DOUBLE_EQ(ScrubScaleFor(PropertyDescriptor{}).Coarse, 1.0);
}
