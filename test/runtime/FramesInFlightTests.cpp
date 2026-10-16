#include <gtest/gtest.h>

#include <graphics/FramesInFlight.h>

// The application can own `imageCount - minImageCount + 1` swapchain images at
// once without blocking forever. Owning fewer than one per frame in flight puts
// the display in the middle of the frame pipeline.
TEST(SwapchainImageCount, LetsTheApplicationOwnOneImagePerFrameInFlight)
{
    for (std::uint32_t minImages : { 1u, 2u, 3u, 4u })
    {
        for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
        {
            const std::uint32_t count = SwapchainImageCount(minImages, 0, frames);
            EXPECT_EQ(count - minImages + 1, frames) << "min " << minImages << " frames " << frames;
        }
    }
}

TEST(SwapchainImageCount, TwoFramesInFlightOnATwoImageMinimumAsksForThree)
{
    EXPECT_EQ(SwapchainImageCount(2, 8, 2), 3u);
    EXPECT_EQ(SwapchainImageCount(3, 0, 2), 4u);
}

TEST(SwapchainImageCount, NeverExceedsTheSurfaceMaximum)
{
    EXPECT_EQ(SwapchainImageCount(2, 2, 2), 2u);
    EXPECT_EQ(SwapchainImageCount(3, 4, 4), 4u);
}

TEST(SwapchainImageCount, AnUnsetFrameCountStillOwnsOneImage)
{
    EXPECT_EQ(SwapchainImageCount(2, 0, 0), 2u);
}
