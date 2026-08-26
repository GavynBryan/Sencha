#include <gtest/gtest.h>

#include <graphics/RetiredSlotQueue.h>

// A descriptor index released this frame is still resolvable by every frame
// already submitted, so recycling it early makes an in-flight draw sample
// whatever the next registration wrote there. These pin the gate that stops
// that -- the policy half of the bindless cache, which needs no device.

TEST(RetiredSlotQueue, HoldsASlotUntilItsStampRetires)
{
    RetiredSlotQueue queue;
    queue.Push(7, 4);

    // Stamped on frame 4: frame 4 itself can still be in flight.
    EXPECT_FALSE(queue.TryPop(GpuFrameRetirement{ .Current = 5, .RetiredThrough = 4 }));
    EXPECT_EQ(queue.PendingCount(), 1u);

    EXPECT_EQ(queue.TryPop(GpuFrameRetirement{ .Current = 6, .RetiredThrough = 5 }), 7u);
    EXPECT_EQ(queue.PendingCount(), 0u);
}

TEST(RetiredSlotQueue, StartsHoldingEverythingBeforeAnyFrameRetires)
{
    // The default clock has proven nothing; a slot released during startup
    // must not come straight back out.
    RetiredSlotQueue queue;
    queue.Push(1, 0);
    EXPECT_FALSE(queue.TryPop(GpuFrameRetirement{}));
}

TEST(RetiredSlotQueue, RecyclesInReleaseOrder)
{
    RetiredSlotQueue queue;
    queue.Push(3, 1);
    queue.Push(9, 1);
    queue.Push(4, 2);

    const GpuFrameRetirement clock{ .Current = 4, .RetiredThrough = 3 };
    EXPECT_EQ(queue.TryPop(clock), 3u);
    EXPECT_EQ(queue.TryPop(clock), 9u);
    EXPECT_EQ(queue.TryPop(clock), 4u);
    EXPECT_FALSE(queue.TryPop(clock));
}

TEST(RetiredSlotQueue, ANewerEntryWaitsBehindAnUnretiredOne)
{
    // FIFO order means the front gates the queue: nothing behind a slot still
    // in flight can be older than it.
    RetiredSlotQueue queue;
    queue.Push(2, 9);
    queue.Push(5, 9);

    const GpuFrameRetirement clock{ .Current = 10, .RetiredThrough = 9 };
    EXPECT_FALSE(queue.TryPop(clock));
    EXPECT_EQ(queue.PendingCount(), 2u);
}

TEST(RetiredSlotQueue, PopIsEmptySafe)
{
    RetiredSlotQueue queue;
    EXPECT_FALSE(queue.TryPop(GpuFrameRetirement{ .Current = 9, .RetiredThrough = 8 }));
    EXPECT_EQ(queue.PendingCount(), 0u);
}

TEST(RetiredSlotQueue, ClearDropsHeldSlots)
{
    RetiredSlotQueue queue;
    queue.Push(1, 0);
    queue.Push(2, 0);
    queue.Clear();
    EXPECT_EQ(queue.PendingCount(), 0u);
}
