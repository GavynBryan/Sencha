#include <gtest/gtest.h>

#include <net/NetSnapshotAck.h>

#include <cstdint>
#include <vector>

//=============================================================================
// Evidence that a snapshot arrived.
//
// The whole value of this type is that it cannot overstate what a client
// holds. Everything below is an attempt to make it overstate something.
//=============================================================================

TEST(NetSnapshotAck, NothingIsConfirmedBeforeAnythingArrives)
{
    NetSnapshotAck ack;
    EXPECT_FALSE(ack.Any());
    EXPECT_FALSE(ack.Confirms(1));
    EXPECT_FALSE(ack.Confirms(0));
    EXPECT_EQ(ack.Newest(), 0u);
}

TEST(NetSnapshotAck, ConfirmsExactlyWhatItWasTold)
{
    NetSnapshotAck ack;
    ack.Observe(10);
    ack.Observe(12);

    EXPECT_TRUE(ack.Confirms(10));
    EXPECT_TRUE(ack.Confirms(12));
    // The gap is the point: 11 was never applied, and a high-water mark alone
    // would have claimed it.
    EXPECT_FALSE(ack.Confirms(11));
    // Nothing ahead of the newest is confirmed either.
    EXPECT_FALSE(ack.Confirms(13));
    EXPECT_EQ(ack.Newest(), 12u);
}

TEST(NetSnapshotAck, OutOfOrderArrivalsStillCount)
{
    NetSnapshotAck ack;
    ack.Observe(20);
    ack.Observe(18);  // Overtook by a newer one, arrived late.

    EXPECT_TRUE(ack.Confirms(18));
    EXPECT_TRUE(ack.Confirms(20));
    EXPECT_FALSE(ack.Confirms(19));
    EXPECT_EQ(ack.Newest(), 20u) << "a late arrival must not move the newest back";
}

TEST(NetSnapshotAck, TheWindowHoldsExactlyItsWidth)
{
    NetSnapshotAck ack;
    ack.Observe(1);
    ack.Observe(1 + NetSnapshotAck::kWindow);

    // The oldest sequence the window can still speak for.
    EXPECT_TRUE(ack.Confirms(1));

    // One further and it falls off the back.
    ack.Observe(2 + NetSnapshotAck::kWindow);
    EXPECT_FALSE(ack.Confirms(1));
}

// A jump larger than the window leaves nothing behind it. Anything else would
// be inventing proof for sequences the mask cannot address.
TEST(NetSnapshotAck, ALongSilenceForgetsRatherThanGuesses)
{
    NetSnapshotAck ack;
    for (std::uint32_t s = 1; s <= 20; ++s)
        ack.Observe(s);
    ASSERT_TRUE(ack.Confirms(5));

    ack.Observe(500);
    EXPECT_EQ(ack.Newest(), 500u);
    for (std::uint32_t s = 1; s <= 20; ++s)
        EXPECT_FALSE(ack.Confirms(s)) << "still claiming " << s;
}

TEST(NetSnapshotAck, MergingIsAUnionAndNeverALoss)
{
    NetSnapshotAck held;
    held.Observe(30);
    held.Observe(28);

    NetSnapshotAck reported;
    reported.Observe(29);
    reported.Observe(27);

    held.Merge(reported);

    EXPECT_TRUE(held.Confirms(27));
    EXPECT_TRUE(held.Confirms(28));
    EXPECT_TRUE(held.Confirms(29));
    EXPECT_TRUE(held.Confirms(30));
    EXPECT_EQ(held.Newest(), 30u);
}

// The command channel is unreliable and unordered. A datagram that arrives
// after a newer one carries an older view, and treating it as current would
// have the authority forget what the newer one proved.
TEST(NetSnapshotAck, AStaleReportCannotUndoANewerOne)
{
    NetSnapshotAck held;
    NetSnapshotAck newer;
    newer.Observe(50);
    newer.Observe(49);
    held.Merge(newer);

    NetSnapshotAck stale;
    stale.Observe(40);
    held.Merge(stale);

    EXPECT_EQ(held.Newest(), 50u);
    EXPECT_TRUE(held.Confirms(49));
    EXPECT_TRUE(held.Confirms(50));
    // And it does add what it genuinely knew.
    EXPECT_TRUE(held.Confirms(40));
}

TEST(NetSnapshotAck, MergingIsOrderIndependent)
{
    const std::vector<std::uint32_t> a{ 100, 98, 95 };
    const std::vector<std::uint32_t> b{ 99, 97, 101 };

    NetSnapshotAck first;
    NetSnapshotAck second;
    for (std::uint32_t s : a) first.Observe(s);
    for (std::uint32_t s : b) second.Observe(s);

    NetSnapshotAck forward = first;
    forward.Merge(second);
    NetSnapshotAck backward = second;
    backward.Merge(first);

    EXPECT_EQ(forward, backward);
    for (std::uint32_t s : { 95u, 97u, 98u, 99u, 100u, 101u })
        EXPECT_TRUE(forward.Confirms(s)) << "lost " << s;
    EXPECT_FALSE(forward.Confirms(96));
}

TEST(NetSnapshotAck, ZeroIsNeverASnapshot)
{
    NetSnapshotAck ack;
    ack.Observe(0);
    EXPECT_FALSE(ack.Any());

    ack.Observe(5);
    ack.Observe(0);
    EXPECT_EQ(ack.Newest(), 5u);
    EXPECT_FALSE(ack.Confirms(0));
}
