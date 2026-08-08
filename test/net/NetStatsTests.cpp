#include <gtest/gtest.h>

#include <net/NetPlayerCommand.h>
#include <net/NetStats.h>
#include <net/PeerCommandRuntime.h>

//=============================================================================
// The traffic surface, and the one knob it exists to let someone turn.
//
// A stats surface that is wrong is worse than none: it is the thing people
// reach for when they already suspect something, and a number that lies sends
// them looking in the wrong place. So the arithmetic is pinned even though
// nothing decides anything from it.
//=============================================================================

namespace
{
    // A hand-run clock. Rates over wall time are exactly the kind of thing a
    // sleep would test badly and a driven clock tests exactly.
    void Advance(NetStats& stats, double& now, double seconds)
    {
        now += seconds;
        stats.Sample(now);
    }
}

TEST(NetStats, ReportsNothingUntilAWindowCloses)
{
    NetStats stats;
    double now = 100.0;
    stats.Sample(now);

    stats.RecordIn(NetTrafficKind::Snapshot, 500);
    Advance(stats, now, 0.5);

    EXPECT_DOUBLE_EQ(stats.TotalIn().Bytes, 0.0)
        << "a half-closed window would report double the real rate";
    EXPECT_TRUE(stats.BytesInHistory().empty());
}

TEST(NetStats, TurnsCountersIntoPerSecondRates)
{
    NetStats stats;
    double now = 0.0;
    stats.Sample(now);

    stats.RecordIn(NetTrafficKind::Snapshot, 600, 60);
    stats.RecordOut(NetTrafficKind::Command, 120, 60);
    Advance(stats, now, 1.0);

    EXPECT_DOUBLE_EQ(stats.In(NetTrafficKind::Snapshot).Bytes, 600.0);
    EXPECT_DOUBLE_EQ(stats.In(NetTrafficKind::Snapshot).Messages, 60.0);
    EXPECT_DOUBLE_EQ(stats.Out(NetTrafficKind::Command).Bytes, 120.0);
    EXPECT_DOUBLE_EQ(stats.TotalIn().Bytes, 600.0);
    EXPECT_DOUBLE_EQ(stats.TotalOut().Bytes, 120.0);
}

// A frame that hitches past the window would otherwise read as a spike that
// never happened, which is the reading most likely to send someone chasing a
// bandwidth problem they do not have.
TEST(NetStats, DividesByTheWindowThatActuallyElapsed)
{
    NetStats stats;
    double now = 0.0;
    stats.Sample(now);

    stats.RecordIn(NetTrafficKind::Snapshot, 1000, 10);
    Advance(stats, now, 4.0);

    EXPECT_DOUBLE_EQ(stats.In(NetTrafficKind::Snapshot).Bytes, 250.0);
    EXPECT_DOUBLE_EQ(stats.In(NetTrafficKind::Snapshot).Messages, 2.5);
}

TEST(NetStats, ClearsTheWindowSoRatesDoNotAccumulate)
{
    NetStats stats;
    double now = 0.0;
    stats.Sample(now);

    stats.RecordIn(NetTrafficKind::Snapshot, 100);
    Advance(stats, now, 1.0);
    ASSERT_DOUBLE_EQ(stats.TotalIn().Bytes, 100.0);

    Advance(stats, now, 1.0);
    EXPECT_DOUBLE_EQ(stats.TotalIn().Bytes, 0.0)
        << "a quiet second has to read as quiet";
    EXPECT_EQ(stats.LifetimeBytesIn(), 100u)
        << "the window clears; the session total does not";
}

TEST(NetStats, HistoryGrowsOldestFirstAndStaysBounded)
{
    NetStats stats;
    double now = 0.0;
    stats.Sample(now);

    for (int second = 1; second <= 3; ++second)
    {
        stats.RecordOut(NetTrafficKind::Snapshot,
                        static_cast<std::size_t>(second) * 100);
        Advance(stats, now, 1.0);
    }

    const std::span<const float> series = stats.BytesOutHistory();
    ASSERT_EQ(series.size(), 3u);
    EXPECT_FLOAT_EQ(series[0], 100.0f);
    EXPECT_FLOAT_EQ(series[2], 300.0f) << "newest belongs at the end, for a plot";

    for (int second = 0; second < 200; ++second)
        Advance(stats, now, 1.0);
    EXPECT_EQ(stats.BytesOutHistory().size(), NetStats::kHistory);
}

TEST(NetStats, PayloadKindsMapToTheirOwnBuckets)
{
    EXPECT_EQ(NetTrafficKindOf(NetPayloadKind::Snapshot), NetTrafficKind::Snapshot);
    EXPECT_EQ(NetTrafficKindOf(NetPayloadKind::Command), NetTrafficKind::Command);
    EXPECT_EQ(NetTrafficKindOf(NetPayloadKind::CVar), NetTrafficKind::CVar);
    // Unattributed traffic is still traffic; dropping it would read as free.
    EXPECT_EQ(NetTrafficKindOf(NetPayloadKind::Invalid), NetTrafficKind::Other);
    EXPECT_EQ(NetTrafficKindOf(static_cast<NetPayloadKind>(200)),
              NetTrafficKind::Other);
}

TEST(NetStats, ResetsWithTheSession)
{
    NetStats stats;
    double now = 0.0;
    stats.Sample(now);
    stats.RecordIn(NetTrafficKind::Command, 64);
    Advance(stats, now, 1.0);
    ASSERT_GT(stats.LifetimeBytesIn(), 0u);

    stats.Reset();
    EXPECT_EQ(stats.LifetimeBytesIn(), 0u);
    EXPECT_DOUBLE_EQ(stats.TotalIn().Bytes, 0.0);
    EXPECT_TRUE(stats.BytesInHistory().empty());
}

//-----------------------------------------------------------------------------
// The knob
//
// Slack was a constant when the number had no surface to be judged against.
// Now that it does, it is the authority's to set, and setting it has to reach
// the buffers that actually hold the input back.
//-----------------------------------------------------------------------------

TEST(NetCommandSlack, ABufferClampsWhatItWillHoldBack)
{
    NetPeerCommandBuffer buffer;
    EXPECT_EQ(buffer.TargetDepth(), NetPeerCommandBuffer::kTargetDepth);

    buffer.SetTargetDepth(4);
    EXPECT_EQ(buffer.TargetDepth(), 4u);

    // Holding back the whole ring would mean the buffer never hands out the
    // records it is dropping the oldest of.
    buffer.SetTargetDepth(1000);
    EXPECT_LT(buffer.TargetDepth(), NetPeerCommandBuffer::kCapacity);
}

TEST(NetCommandSlack, ZeroSlackConsumesAsFastAsRecordsArrive)
{
    NetPeerCommandBuffer buffer;
    buffer.SetTargetDepth(0);

    NetPlayerCommand command;
    command.RecordCount = 1;
    command.Records[0].Tick = 1;
    command.Records[0].ActionCount = 1;
    buffer.Receive(command);

    NetCommandRecord record;
    ASSERT_TRUE(buffer.Next(record));
    EXPECT_EQ(buffer.QueuedTicks(), 0u);
}
