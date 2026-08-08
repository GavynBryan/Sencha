#include <gtest/gtest.h>

#include <net/NetPlayerCommand.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

//=============================================================================
// The input channel's value layer: what a command is on the wire, and how one
// peer's arriving commands meet the tick rate that consumes them.
//
// Everything here is pure. A command crossing this boundary has come from a
// machine nobody controls, so the decoder is written to be read by a reviewer
// and every field a peer can set is bounded here rather than downstream.
//=============================================================================

namespace
{
    InputActionValue Digital(bool held, bool pressed, bool fired)
    {
        InputActionValue value;
        InputActionFlags flags = InputActionFlags::None;
        if (held)
            flags |= InputActionFlags::Held;
        if (pressed)
            flags |= InputActionFlags::Pressed;
        if (fired)
            flags |= InputActionFlags::Fired;
        value.Flags = flags;
        return value;
    }

    InputActionValue Axis(float x, float y)
    {
        InputActionValue value;
        value.X = x;
        value.Y = y;
        return value;
    }

    NetCommandRecord RecordAt(std::uint64_t tick, std::uint8_t actions)
    {
        NetCommandRecord record;
        record.Tick = tick;
        record.ActionCount = actions;
        return record;
    }

    // A command carrying `count` consecutive ticks ending at `newest`, each with
    // a two-action vocabulary: an axis and a button.
    NetPlayerCommand CommandEndingAt(std::uint64_t newest, std::uint8_t count)
    {
        NetPlayerCommand command;
        command.Yaw = 1.25f;
        command.Pitch = -0.5f;
        command.RecordCount = count;
        for (std::uint8_t index = 0; index < count; ++index)
        {
            NetCommandRecord& record = command.Records[index];
            record = RecordAt(newest - index, 2);
            record.Actions[0] = Axis(0.5f, -0.25f);
            record.Actions[1] = Digital(true, index == count - 1, true);
        }
        return command;
    }

    // Encodes into a datagram-sized buffer and decodes it back.
    struct RoundTrip
    {
        std::array<std::byte, 1024> Buffer{};
        std::size_t Encoded = 0;
        NetPlayerCommand Decoded;
        bool Ok = false;

        explicit RoundTrip(const NetPlayerCommand& command,
                           std::size_t capacityBytes = 1024)
        {
            NetBitWriter writer(std::span<std::byte>(Buffer).subspan(0, capacityBytes));
            Encoded = NetEncodePlayerCommand(command, writer);
            if (Encoded == 0 || writer.Overflowed())
                return;
            NetBitReader reader(
                std::span<const std::byte>(Buffer).subspan(0, writer.BytesWritten()));
            Ok = NetDecodePlayerCommand(reader, Decoded);
        }
    };
}

//-----------------------------------------------------------------------------
// Codec
//-----------------------------------------------------------------------------

TEST(NetPlayerCommandCodec, RoundTripsAimAndEveryRecord)
{
    const NetPlayerCommand sent = CommandEndingAt(1000, 4);
    const RoundTrip trip(sent);

    ASSERT_TRUE(trip.Ok);
    EXPECT_EQ(trip.Encoded, 4u);
    ASSERT_EQ(trip.Decoded.RecordCount, 4);
    EXPECT_FLOAT_EQ(trip.Decoded.Yaw, sent.Yaw);
    EXPECT_FLOAT_EQ(trip.Decoded.Pitch, sent.Pitch);

    for (std::size_t index = 0; index < 4; ++index)
    {
        const NetCommandRecord& in = sent.Records[index];
        const NetCommandRecord& out = trip.Decoded.Records[index];
        EXPECT_EQ(out.Tick, in.Tick) << "record " << index;
        ASSERT_EQ(out.ActionCount, in.ActionCount) << "record " << index;
        EXPECT_FLOAT_EQ(out.Actions[0].X, in.Actions[0].X) << "record " << index;
        EXPECT_FLOAT_EQ(out.Actions[0].Y, in.Actions[0].Y) << "record " << index;
        EXPECT_EQ(static_cast<std::uint8_t>(out.Actions[1].Flags),
                  static_cast<std::uint8_t>(in.Actions[1].Flags))
            << "record " << index;
    }
}

// A button carries no axis, and the encoder does not spend the bits. The saving
// is what makes an eight-tick redundancy window affordable on every command.
TEST(NetPlayerCommandCodec, ButtonsCostNoAxisBits)
{
    NetPlayerCommand buttons;
    buttons.RecordCount = 1;
    buttons.Records[0] = RecordAt(1, 8);
    for (std::uint8_t index = 0; index < 8; ++index)
        buttons.Records[0].Actions[index] = Digital(true, false, true);

    NetPlayerCommand axes = buttons;
    for (std::uint8_t index = 0; index < 8; ++index)
        axes.Records[0].Actions[index] = Axis(1.0f, 1.0f);

    std::array<std::byte, 1024> a{};
    std::array<std::byte, 1024> b{};
    NetBitWriter buttonWriter(a);
    NetBitWriter axisWriter(b);
    ASSERT_EQ(NetEncodePlayerCommand(buttons, buttonWriter), 1u);
    ASSERT_EQ(NetEncodePlayerCommand(axes, axisWriter), 1u);

    EXPECT_EQ(axisWriter.BitsWritten() - buttonWriter.BitsWritten(), 8u * 64u);
}

// Redundancy is a courtesy and the newest record is the message, so a buffer
// that cannot hold the window drops the oldest rather than the whole command.
TEST(NetPlayerCommandCodec, DropsOldRedundancyRatherThanTheCommand)
{
    const NetPlayerCommand sent = CommandEndingAt(500, 8);

    // Sized to fit the header and roughly two records.
    const RoundTrip trip(sent, 32);

    ASSERT_TRUE(trip.Ok);
    EXPECT_GE(trip.Encoded, 1u);
    EXPECT_LT(trip.Encoded, 8u) << "the buffer cannot hold the whole window";
    EXPECT_EQ(trip.Decoded.RecordCount, trip.Encoded);
    EXPECT_EQ(trip.Decoded.Records[0].Tick, 500u)
        << "the newest tick is the one that must survive";
}

TEST(NetPlayerCommandCodec, WritesNothingWhenEvenTheNewestRecordDoesNotFit)
{
    std::array<std::byte, 4> tiny{};
    NetBitWriter writer(tiny);
    EXPECT_EQ(NetEncodePlayerCommand(CommandEndingAt(7, 3), writer), 0u);
    EXPECT_EQ(writer.BitsWritten(), 0u)
        << "a header written for records that do not fit would claim input the "
           "message does not carry";
}

TEST(NetPlayerCommandCodec, RefusesATruncatedMessage)
{
    const NetPlayerCommand sent = CommandEndingAt(90, 3);
    std::array<std::byte, 1024> buffer{};
    NetBitWriter writer(buffer);
    ASSERT_GT(NetEncodePlayerCommand(sent, writer), 0u);

    for (std::size_t bytes = 0; bytes < writer.BytesWritten(); ++bytes)
    {
        NetBitReader reader(
            std::span<const std::byte>(buffer).subspan(0, bytes));
        NetPlayerCommand out;
        EXPECT_FALSE(NetDecodePlayerCommand(reader, out))
            << "accepted a message cut to " << bytes << " bytes";
    }
}

TEST(NetPlayerCommandCodec, RefusesNonFiniteAim)
{
    NetPlayerCommand sent = CommandEndingAt(3, 1);
    sent.Pitch = std::numeric_limits<float>::quiet_NaN();

    std::array<std::byte, 1024> buffer{};
    NetBitWriter writer(buffer);
    ASSERT_GT(NetEncodePlayerCommand(sent, writer), 0u);

    NetBitReader reader(
        std::span<const std::byte>(buffer).subspan(0, writer.BytesWritten()));
    NetPlayerCommand out;
    EXPECT_FALSE(NetDecodePlayerCommand(reader, out));
}

TEST(NetPlayerCommandCodec, RefusesNonFiniteAxes)
{
    NetPlayerCommand sent = CommandEndingAt(3, 1);
    sent.Records[0].Actions[0] = Axis(std::numeric_limits<float>::infinity(), 0.0f);

    std::array<std::byte, 1024> buffer{};
    NetBitWriter writer(buffer);
    ASSERT_GT(NetEncodePlayerCommand(sent, writer), 0u);

    NetBitReader reader(
        std::span<const std::byte>(buffer).subspan(0, writer.BytesWritten()));
    NetPlayerCommand out;
    EXPECT_FALSE(NetDecodePlayerCommand(reader, out));
}

// Nothing a peer sends may size an allocation or an index. Every bit pattern is
// fed through the decoder; the only requirement is that it never reports
// success with a count past the cap.
TEST(NetPlayerCommandCodec, NoBitPatternExceedsTheCaps)
{
    std::vector<std::byte> buffer(96);
    std::uint32_t state = 0x1234567u;
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        for (std::byte& byte : buffer)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<std::byte>((state >> 16) & 0xFFu);
        }

        NetBitReader reader(buffer);
        NetPlayerCommand out;
        if (!NetDecodePlayerCommand(reader, out))
            continue;

        ASSERT_LE(out.RecordCount, kNetMaxCommandRecords);
        ASSERT_GT(out.RecordCount, 0);
        for (const NetCommandRecord& record : out.Ticks())
        {
            ASSERT_LE(record.ActionCount, kNetMaxCommandActions);
            ASSERT_LE(record.Tick, out.Records[0].Tick)
                << "a record claimed to be newer than the command's newest tick";
        }
    }
}

//-----------------------------------------------------------------------------
// Arrival buffer
//-----------------------------------------------------------------------------

TEST(NetPeerCommandBuffer, HasNoInputBeforeAPeerHasSpoken)
{
    NetPeerCommandBuffer buffer;
    NetCommandRecord record;
    EXPECT_FALSE(buffer.Next(record))
        << "a silent peer has no input, which is not the same as zeroed input";
    EXPECT_FALSE(buffer.HasAim());
}

TEST(NetPeerCommandBuffer, HandsOutOneRecordPerTickInOrder)
{
    NetPeerCommandBuffer buffer;
    buffer.Receive(CommandEndingAt(12, 3));  // ticks 10, 11, 12

    for (std::uint64_t tick = 10; tick <= 12; ++tick)
    {
        NetCommandRecord record;
        ASSERT_TRUE(buffer.Next(record)) << "tick " << tick;
        EXPECT_EQ(record.Tick, tick);
    }
    EXPECT_EQ(buffer.QueuedTicks(), 0u);
}

// The redundancy window repeats ticks the authority already simulated. Applying
// one twice would replay a player's input, so arriving again has to be free.
TEST(NetPeerCommandBuffer, IgnoresTicksItHasAlreadyHandedOut)
{
    NetPeerCommandBuffer buffer;
    buffer.Receive(CommandEndingAt(20, 4));  // 17..20

    NetCommandRecord record;
    ASSERT_TRUE(buffer.Next(record));
    ASSERT_EQ(record.Tick, 17u);

    // The next command resends 17 and 18 alongside 21 and 22.
    buffer.Receive(CommandEndingAt(22, 6));  // 17..22

    for (std::uint64_t tick = 18; tick <= 22; ++tick)
    {
        ASSERT_TRUE(buffer.Next(record)) << "tick " << tick;
        EXPECT_EQ(record.Tick, tick);
    }
    EXPECT_EQ(buffer.QueuedTicks(), 0u)
        << "the resent ticks were credited twice";
}

TEST(NetPeerCommandBuffer, OrdersRecordsThatArriveOutOfOrder)
{
    NetPeerCommandBuffer buffer;
    buffer.Receive(CommandEndingAt(40, 1));  // 40
    buffer.Receive(CommandEndingAt(38, 1));  // 38
    buffer.Receive(CommandEndingAt(39, 1));  // 39

    NetCommandRecord record;
    for (std::uint64_t tick = 38; tick <= 40; ++tick)
    {
        ASSERT_TRUE(buffer.Next(record));
        EXPECT_EQ(record.Tick, tick);
    }
}

// A peer that floods buys itself nothing but its own latency, and must not be
// able to buy memory on the authority.
TEST(NetPeerCommandBuffer, BoundsHowFarAheadAPeerMayRun)
{
    NetPeerCommandBuffer buffer;
    for (std::uint64_t tick = 1; tick <= 200; ++tick)
    {
        NetPlayerCommand command;
        command.RecordCount = 1;
        command.Records[0] = RecordAt(tick, 1);
        buffer.Receive(command);
    }

    EXPECT_LE(buffer.QueuedTicks(), NetPeerCommandBuffer::kCapacity);

    NetCommandRecord record;
    ASSERT_TRUE(buffer.Next(record));
    EXPECT_EQ(record.Tick, 200u - NetPeerCommandBuffer::kCapacity + 1u)
        << "the oldest queued tick is the one worth dropping";
}

//-----------------------------------------------------------------------------
// Gap policy
//
// What a starved tick does is the whole reason input rides an unreliable
// channel: it has to degrade into something playable rather than into a stop.
//-----------------------------------------------------------------------------

TEST(NetPeerCommandBufferGap, HoldsHeldActionsWhenNothingArrives)
{
    NetPeerCommandBuffer buffer;
    NetPlayerCommand command;
    command.RecordCount = 1;
    command.Records[0] = RecordAt(5, 2);
    command.Records[0].Actions[0] = Axis(0.0f, 1.0f);
    command.Records[0].Actions[1] = Digital(true, false, true);
    buffer.Receive(command);

    NetCommandRecord record;
    ASSERT_TRUE(buffer.Next(record));

    for (int starved = 0; starved < 5; ++starved)
    {
        ASSERT_TRUE(buffer.Next(record)) << "starved tick " << starved;
        EXPECT_FLOAT_EQ(record.Actions[0].Y, 1.0f)
            << "a player holding forward through a dropped datagram is still "
               "holding forward";
        EXPECT_TRUE(record.Actions[1].IsHeld());
        EXPECT_TRUE(record.Actions[1].WasFired())
            << "a hold-mode action still fires while it is held";
    }
    EXPECT_EQ(buffer.StarvedTicks(), 5u);
}

TEST(NetPeerCommandBufferGap, DoesNotRepeatEdges)
{
    NetPeerCommandBuffer buffer;
    NetPlayerCommand command;
    command.RecordCount = 1;
    command.Records[0] = RecordAt(9, 1);
    // A tap: pressed and fired, but not held. Repeating it would fire the same
    // request again on every tick the connection is behind.
    command.Records[0].Actions[0] = Digital(false, true, true);
    buffer.Receive(command);

    NetCommandRecord record;
    ASSERT_TRUE(buffer.Next(record));
    ASSERT_TRUE(record.Actions[0].WasPressed());

    ASSERT_TRUE(buffer.Next(record));
    EXPECT_FALSE(record.Actions[0].WasPressed());
    EXPECT_FALSE(record.Actions[0].WasFired());
    EXPECT_FALSE(record.Actions[0].IsHeld());
}

TEST(NetPeerCommandBufferGap, ResumesFromTheWireWhenTrafficReturns)
{
    NetPeerCommandBuffer buffer;
    NetPlayerCommand held;
    held.RecordCount = 1;
    held.Records[0] = RecordAt(1, 1);
    held.Records[0].Actions[0] = Digital(true, true, true);
    buffer.Receive(held);

    NetCommandRecord record;
    ASSERT_TRUE(buffer.Next(record));
    ASSERT_TRUE(buffer.Next(record));  // starved, still held

    NetPlayerCommand released;
    released.RecordCount = 1;
    released.Records[0] = RecordAt(2, 1);
    released.Records[0].Actions[0] = InputActionValue{};
    buffer.Receive(released);

    ASSERT_TRUE(buffer.Next(record));
    EXPECT_EQ(record.Tick, 2u);
    EXPECT_FALSE(record.Actions[0].IsHeld())
        << "the hold has to end when the wire says it ended";
}

TEST(NetPeerCommandBufferGap, KeepsTheNewestAimWhenAnOlderCommandOvertakesIt)
{
    NetPeerCommandBuffer buffer;

    NetPlayerCommand newer = CommandEndingAt(60, 1);
    newer.Yaw = 2.0f;
    buffer.Receive(newer);

    NetPlayerCommand older = CommandEndingAt(59, 1);
    older.Yaw = 1.0f;
    buffer.Receive(older);

    EXPECT_TRUE(buffer.HasAim());
    EXPECT_FLOAT_EQ(buffer.Yaw(), 2.0f)
        << "a datagram that overtakes a newer one must not drag the view back";
}
