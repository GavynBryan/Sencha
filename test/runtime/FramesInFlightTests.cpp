#include <gtest/gtest.h>

#include <graphics/FramesInFlight.h>

namespace
{
    constexpr bool kImagesBoundTheQueue = true;
    constexpr bool kSomethingElseBoundsTheQueue = false;
}

// The application can own `imageCount - minImageCount + 1` swapchain images at
// once without blocking forever. Owning fewer than one per frame in flight puts
// the display in the middle of the frame pipeline.
TEST(SwapchainImageCount, LetsTheApplicationOwnOneImagePerFrameInFlight)
{
    for (std::uint32_t minImages : { 1u, 2u, 3u, 4u })
    {
        for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
        {
            for (const bool imagesBound : { kImagesBoundTheQueue, kSomethingElseBoundsTheQueue })
            {
                const std::uint32_t count = SwapchainImageCount(minImages, 0, frames, imagesBound);
                EXPECT_GE(count - minImages + 1, frames) << "min " << minImages << " frames " << frames;
            }
        }
    }
}

// Without a display queue limit every image beyond the fewest is one more
// present queued ahead of the display, a refresh of latency each.
TEST(SwapchainImageCount, WhereImagesBoundTheQueueAsksForTheFewest)
{
    EXPECT_EQ(SwapchainImageCount(2, 8, 2, kImagesBoundTheQueue), 3u);
    EXPECT_EQ(SwapchainImageCount(3, 0, 2, kImagesBoundTheQueue), 4u);
}

// With one, images bound throughput only, and one more keeps a path that
// holds an image past its replacement from stalling acquire.
TEST(SwapchainImageCount, WhereSomethingElseBoundsTheQueueAsksForOneMore)
{
    EXPECT_EQ(SwapchainImageCount(2, 8, 2, kSomethingElseBoundsTheQueue), 4u);
    EXPECT_EQ(SwapchainImageCount(3, 0, 2, kSomethingElseBoundsTheQueue), 5u);
}

TEST(SwapchainImageCount, NeverExceedsTheSurfaceMaximum)
{
    EXPECT_EQ(SwapchainImageCount(2, 2, 2, kImagesBoundTheQueue), 2u);
    EXPECT_EQ(SwapchainImageCount(3, 4, 4, kImagesBoundTheQueue), 4u);
    EXPECT_EQ(SwapchainImageCount(2, 3, 2, kSomethingElseBoundsTheQueue), 3u);
}

TEST(SwapchainImageCount, AnUnsetFrameCountStillOwnsOneImage)
{
    EXPECT_EQ(SwapchainImageCount(2, 0, 0, kImagesBoundTheQueue), 2u);
}
