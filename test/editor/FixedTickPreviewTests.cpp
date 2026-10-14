// The clock a private-World preview runs on: tick zero from a rebuild, a bounded
// record per tick, rewind as a replay from zero, and one branch per paused tick.

#include "preview/FixedTickPreview.h"

#include <gtest/gtest.h>

namespace
{
struct Counted
{
    int Builds = 0;
    std::vector<std::uint64_t> Ran;
    bool Runnable = true;

    FixedTickPreview<std::uint64_t> Clock{ { .Rebuild = [this] { ++Builds; Ran.clear(); return Runnable; },
                                             .CanRun = [this] { return Runnable; },
                                             .Run = [this](std::uint64_t tick) { Ran.push_back(tick); return tick; } } };
};
}

TEST(FixedTickPreview, ARestartRebuildsAndRunsTickZero)
{
    Counted preview;
    preview.Clock.Restart();
    EXPECT_EQ(preview.Builds, 1);
    EXPECT_EQ(preview.Ran, (std::vector<std::uint64_t>{ 0 }));
    preview.Clock.Step();
    EXPECT_EQ(preview.Clock.Now(), 1u);
    EXPECT_EQ(preview.Clock.History().back(), 1u);
}

TEST(FixedTickPreview, RunningToAnEarlierTickReplaysFromZero)
{
    Counted preview;
    preview.Clock.Restart();
    preview.Clock.RunTo(10);
    preview.Clock.RunTo(4);
    EXPECT_EQ(preview.Builds, 2);
    EXPECT_EQ(preview.Ran, (std::vector<std::uint64_t>{ 0, 1, 2, 3, 4 }));
    preview.Clock.Replay();
    EXPECT_EQ(preview.Builds, 3) << "a replay rebuilds";
    EXPECT_EQ(preview.Clock.Now(), 4u) << "and comes back to where it was";
}

TEST(FixedTickPreview, AWorldThatCannotRunTakesNoTicks)
{
    Counted preview;
    preview.Runnable = false;
    preview.Clock.Restart();
    preview.Clock.Play();
    preview.Clock.Advance(10.0, 60);
    preview.Clock.RunTo(5);
    EXPECT_TRUE(preview.Ran.empty());
    EXPECT_TRUE(preview.Clock.History().empty());
}

TEST(FixedTickPreview, PlayingSchedulesTicksAtTheSpeedAndDropsAStall)
{
    Counted preview;
    preview.Clock.Restart();
    preview.Clock.Play();
    ASSERT_TRUE(preview.Clock.SetSpeed(0.5));
    preview.Clock.Advance(4.0 / 60.0, 60);
    EXPECT_EQ(preview.Clock.Now(), 2u) << "half speed: two ticks for four ticks of wall time";
    preview.Clock.Advance(10.0, 60);
    EXPECT_EQ(preview.Clock.Now(), 2u + FixedTickPreview<std::uint64_t>::kMaxTicksPerAdvance);
    EXPECT_FALSE(preview.Clock.SetSpeed(0.0));
}

TEST(FixedTickPreview, EditsOnOnePausedTickShareOneBranch)
{
    Counted preview;
    preview.Clock.Restart();
    preview.Clock.RunTo(3);
    EXPECT_TRUE(preview.Clock.Branch());
    EXPECT_FALSE(preview.Clock.Branch()) << "the second edit on tick 3 joins the first";
    preview.Clock.Step();
    EXPECT_TRUE(preview.Clock.Branch()) << "a new tick, a new branch";
}

TEST(FixedTickPreview, HistoryKeepsTheLatestTicks)
{
    Counted preview;
    preview.Clock.Restart();
    preview.Clock.RunTo(FixedTickPreview<std::uint64_t>::kHistoryCapacity + 10);
    EXPECT_EQ(preview.Clock.History().size(), FixedTickPreview<std::uint64_t>::kHistoryCapacity);
    EXPECT_EQ(preview.Clock.History().front(), 11u);
}
