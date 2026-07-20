// TickEventChannel<T> publication contract: BeginTick clears the previous
// tick's events and promotes staged ones; staged events survive frames that
// run zero ticks; consumers never observe half a tick.

#include <core/event/TickEventChannel.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
struct Ended
{
    int Id = 0;
};
} // namespace

TEST(TickEventChannel, PublishIsVisibleWithinTheTick)
{
    TickEventChannel<Ended> channel;

    channel.BeginTick();
    channel.Publish(Ended{ 1 });
    channel.Publish(Ended{ 2 });

    ASSERT_EQ(channel.Size(), 2u);
    EXPECT_EQ(channel.Items()[0].Id, 1);
    EXPECT_EQ(channel.Items()[1].Id, 2);
}

TEST(TickEventChannel, BeginTickClearsThePreviousTick)
{
    TickEventChannel<Ended> channel;

    channel.BeginTick();
    channel.Publish(Ended{ 1 });
    channel.BeginTick();

    EXPECT_TRUE(channel.Empty());
}

TEST(TickEventChannel, StagedEventsSurfaceAtNextBeginTick)
{
    TickEventChannel<Ended> channel;

    channel.Stage(Ended{ 7 });
    EXPECT_TRUE(channel.Empty()) << "staged events must not be visible early";
    EXPECT_EQ(channel.PendingCount(), 1u);

    channel.BeginTick();

    ASSERT_EQ(channel.Size(), 1u);
    EXPECT_EQ(channel.Items()[0].Id, 7);
    EXPECT_EQ(channel.PendingCount(), 0u);
}

TEST(TickEventChannel, StagedEventsSurviveZeroTickFrames)
{
    TickEventChannel<Ended> channel;

    // A frame with no fixed ticks never calls BeginTick; the stage sits.
    channel.Stage(Ended{ 3 });
    channel.Stage(Ended{ 4 });

    // Next frame that executes a tick sees both, ahead of that tick's own
    // publications, in stage order.
    channel.BeginTick();
    channel.Publish(Ended{ 5 });

    ASSERT_EQ(channel.Size(), 3u);
    EXPECT_EQ(channel.Items()[0].Id, 3);
    EXPECT_EQ(channel.Items()[1].Id, 4);
    EXPECT_EQ(channel.Items()[2].Id, 5);
}

TEST(TickEventChannel, MultiTickFrameScopesEventsPerTick)
{
    TickEventChannel<Ended> channel;

    channel.BeginTick();
    channel.Publish(Ended{ 1 });
    ASSERT_EQ(channel.Size(), 1u);

    // Second tick of the same frame: tick one's events are gone. Frame-lane
    // consumers reading after the frame see only the last tick.
    channel.BeginTick();
    channel.Publish(Ended{ 2 });

    ASSERT_EQ(channel.Size(), 1u);
    EXPECT_EQ(channel.Items()[0].Id, 2);
}

TEST(TickEventChannel, StageDuringTickHoldsUntilNextTick)
{
    TickEventChannel<Ended> channel;

    channel.BeginTick();
    channel.Publish(Ended{ 1 });
    channel.Stage(Ended{ 2 }); // e.g. raised at a boundary mid-frame

    ASSERT_EQ(channel.Size(), 1u);

    channel.BeginTick();

    ASSERT_EQ(channel.Size(), 1u);
    EXPECT_EQ(channel.Items()[0].Id, 2);
}

TEST(TickEventChannel, MoveOnlyPayloadsCompile)
{
    TickEventChannel<std::string> channel;

    channel.Stage(std::string("staged"));
    channel.BeginTick();
    channel.Publish(std::string("published"));

    ASSERT_EQ(channel.Size(), 2u);
    EXPECT_EQ(channel.Items()[0], "staged");
    EXPECT_EQ(channel.Items()[1], "published");
}
