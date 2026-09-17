#include <gtest/gtest.h>

#include <input/InputFrame.h>
#include <input/PointerCaptureSettle.h>

#include <SDL3/SDL.h>

// Relative mouse mode teleports the cursor when it is entered or left, and the
// platform reports that jump as ordinary relative motion. Reading it as input
// turns the view by however far the cursor had wandered -- which is what a
// camera snapping to the mouse on resume actually is.

TEST(PointerCaptureSettleTest, NothingIsDroppedWhileCaptureIsSteady)
{
    PointerCaptureSettle settle;
    for (int frame = 0; frame < 10; ++frame)
    {
        EXPECT_FALSE(settle.ShouldDropPointerMotion()) << "frame " << frame;
        settle.EndFrame();
    }
}

TEST(PointerCaptureSettleTest, AChangeCoversTheFrameItLandsOnAndTheNextOne)
{
    // Two, because the change can land either side of the platform pump -- the
    // shell applies it while resolving a transition, focus applies it during
    // the pump -- and the motion it produces arrives on the pump after that.
    PointerCaptureSettle settle;
    settle.NotifyChanged();

    EXPECT_TRUE(settle.ShouldDropPointerMotion());
    settle.EndFrame();
    EXPECT_TRUE(settle.ShouldDropPointerMotion());
    settle.EndFrame();
    EXPECT_FALSE(settle.ShouldDropPointerMotion())
        << "a steady capture kept discarding the player's own input";
}

TEST(PointerCaptureSettleTest, ASecondChangeRearmsRatherThanAccumulating)
{
    // Toggling twice in quick succession -- alt-tabbing out and straight back --
    // covers the second change, and does not add up to a long blind spell.
    PointerCaptureSettle settle;
    settle.NotifyChanged();
    settle.EndFrame();
    settle.NotifyChanged();

    EXPECT_EQ(settle.FramesRemaining(), 2u);
    settle.EndFrame();
    settle.EndFrame();
    EXPECT_FALSE(settle.ShouldDropPointerMotion());
}

TEST(PointerCaptureSettleTest, DroppingPointerMotionLeavesEverythingElseAlone)
{
    // The cursor moved without the player moving it; the buttons and keys are
    // still exactly where the player left them.
    InputFrame frame;
    frame.SetKeyHeld(SDL_SCANCODE_W, true);
    frame.SetMouseButtonHeld(SDL_BUTTON_LEFT, true);
    frame.MouseDeltaX = 812.0f;
    frame.MouseDeltaY = -430.0f;
    frame.MouseWheelY = 3.0f;

    frame.DropPointerMotion();

    EXPECT_FLOAT_EQ(frame.MouseDeltaX, 0.0f);
    EXPECT_FLOAT_EQ(frame.MouseDeltaY, 0.0f);
    // A capture change moves the cursor, not the wheel, so a scroll aimed at
    // the frame a menu closes still lands.
    EXPECT_FLOAT_EQ(frame.MouseWheelY, 3.0f);
    EXPECT_TRUE(frame.IsKeyDown(SDL_SCANCODE_W));
    EXPECT_TRUE(frame.IsMouseButtonDown(SDL_BUTTON_LEFT));
}

TEST(PointerCaptureSettleTest, TheJumpAroundAResumeNeverReachesTheSnapshot)
{
    // The reported bug, as a sequence. The menu is up and the cursor is free;
    // the player moves it across the screen and clicks Resume; capture is
    // re-acquired and the pointer is teleported back. The frame that carries
    // that teleport must hand the view nothing.
    PointerCaptureSettle settle;
    InputFrame frame;

    const auto pump = [&](float dx, float dy) {
        frame.MouseDeltaX = dx;
        frame.MouseDeltaY = dy;
        if (settle.ShouldDropPointerMotion())
            frame.DropPointerMotion();
        settle.EndFrame();
        return frame.MouseDeltaX;
    };

    // Playing: the player's own movement reaches the view.
    EXPECT_FLOAT_EQ(pump(4.0f, 2.0f), 4.0f);

    // Resume re-acquires capture, in the frame's update.
    settle.NotifyChanged();

    // The pump that follows carries the teleport.
    EXPECT_FLOAT_EQ(pump(-940.0f, 512.0f), 0.0f)
        << "the cursor's jump back to the centre turned the camera";
    EXPECT_FLOAT_EQ(pump(0.0f, 0.0f), 0.0f);

    // And the player is driving again immediately after.
    EXPECT_FLOAT_EQ(pump(3.0f, -1.0f), 3.0f)
        << "the player's own input was still being discarded";
}
