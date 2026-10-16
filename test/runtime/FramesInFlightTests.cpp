#include <gtest/gtest.h>

#include <graphics/FramesInFlight.h>

TEST(MaxQueuedPresents, IsOnePerFrameInFlight)
{
    for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
        EXPECT_EQ(MaxQueuedPresents(frames), frames);
    EXPECT_EQ(MaxQueuedPresents(0), kMinFramesInFlight);
}

// One image on the display, one per present allowed to queue behind it, and
// the one being recorded: fewer and acquire binds before the queue bound does.
TEST(SwapchainImageCount, HoldsTheDisplayedImageTheQueueAndTheOneRecording)
{
    for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
        EXPECT_EQ(SwapchainImageCount(2, 0, frames), frames + 2) << "frames " << frames;

    // Mesa's Wayland surfaces report a minimum of three; NVIDIA's report two.
    EXPECT_EQ(SwapchainImageCount(3, 0, 2), 4u);
    EXPECT_EQ(SwapchainImageCount(2, 8, 2), 4u);
}

TEST(SwapchainImageCount, NeverAsksForFewerThanTheSurfaceMinimum)
{
    EXPECT_EQ(SwapchainImageCount(5, 0, 2), 5u);
    EXPECT_EQ(SwapchainImageCount(4, 0, 1), 4u);
}

TEST(SwapchainImageCount, NeverExceedsTheSurfaceMaximum)
{
    EXPECT_EQ(SwapchainImageCount(2, 3, 2), 3u);
    EXPECT_EQ(SwapchainImageCount(3, 4, 4), 4u);
}

TEST(SwapchainImageCount, AnUnsetFrameCountMeansOneFrameInFlight)
{
    EXPECT_EQ(SwapchainImageCount(2, 0, 0), 3u);
}
