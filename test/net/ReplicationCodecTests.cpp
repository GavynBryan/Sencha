#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationLayout.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace
{
    constexpr std::size_t kScratchBytes = 512;

    std::span<const std::byte> BytesOf(const auto& value)
    {
        return { reinterpret_cast<const std::byte*>(&value), sizeof(value) };
    }

    std::span<std::byte> MutableBytesOf(auto& value)
    {
        return { reinterpret_cast<std::byte*>(&value), sizeof(value) };
    }

    ReplicationLayout EngineLayout()
    {
        ReplicationLayout layout;
        ComponentRegistrar components(nullptr, nullptr, &layout);
        RegisterEngineComponents(components);
        return layout;
    }

    const ReplicatedComponent& LookComponent(const ReplicationLayout& layout)
    {
        const ReplicatedComponent* component =
            layout.Find(ResolveComponentTypeId<LookOrientation>());
        EXPECT_NE(component, nullptr);
        return *component;
    }

    // Encode `current` against `baseline`, then apply the result onto `target`.
    // Returns the bits the encoding took, so tests can assert on cost as well
    // as on correctness.
    std::size_t RoundTrip(const ReplicatedComponent& component,
                          std::span<const std::byte> current,
                          std::span<const std::byte> baseline,
                          std::span<std::byte> target,
                          bool forOwner = true)
    {
        std::array<std::byte, kScratchBytes> scratch{};
        NetBitWriter writer(scratch);
        EXPECT_TRUE(ReplicationEncodeComponent(component, current, baseline,
                                               forOwner, writer));
        EXPECT_FALSE(writer.Overflowed());

        NetBitReader reader(writer.Written());
        EXPECT_TRUE(ReplicationDecodeComponent(component, reader, target));
        return writer.BitsWritten();
    }
}

//=============================================================================
// Bit packing
//=============================================================================

TEST(NetBitStream, RoundTripsArbitraryWidths)
{
    std::array<std::byte, 64> scratch{};
    NetBitWriter writer(scratch);

    // Deliberately not byte-aligned at any point.
    writer.WriteBits(1, 1);
    writer.WriteBits(5, 3);
    writer.WriteBits(1000, 12);
    writer.WriteBits(0xABCDEF, 24);
    writer.WriteBool(false);
    writer.WriteU32(0xDEADBEEF);
    writer.WriteU64(0x0123456789ABCDEFull);
    ASSERT_FALSE(writer.Overflowed());

    NetBitReader reader(writer.Written());
    std::uint32_t a = 0, b = 0, c = 0, d = 0, e = 0;
    bool flag = true;
    std::uint64_t wide = 0;
    ASSERT_TRUE(reader.ReadBits(1, a));
    ASSERT_TRUE(reader.ReadBits(3, b));
    ASSERT_TRUE(reader.ReadBits(12, c));
    ASSERT_TRUE(reader.ReadBits(24, d));
    ASSERT_TRUE(reader.ReadBool(flag));
    ASSERT_TRUE(reader.ReadBits(32, e));
    ASSERT_TRUE(reader.ReadU64(wide));

    EXPECT_EQ(a, 1u);
    EXPECT_EQ(b, 5u);
    EXPECT_EQ(c, 1000u);
    EXPECT_EQ(d, 0xABCDEFu);
    EXPECT_FALSE(flag);
    EXPECT_EQ(e, 0xDEADBEEFu);
    EXPECT_EQ(wide, 0x0123456789ABCDEFull);
}

TEST(NetBitStream, FloatsAndDoublesSurviveExactly)
{
    std::array<std::byte, 64> scratch{};
    NetBitWriter writer(scratch);
    writer.WriteBits(1, 3);  // knock it out of byte alignment first
    writer.WriteFloat(-1234.5678f);
    writer.WriteDouble(3.14159265358979);

    NetBitReader reader(writer.Written());
    std::uint32_t skip = 0;
    float single = 0.0f;
    double wide = 0.0;
    ASSERT_TRUE(reader.ReadBits(3, skip));
    ASSERT_TRUE(reader.ReadFloat(single));
    ASSERT_TRUE(reader.ReadDouble(wide));

    EXPECT_EQ(single, -1234.5678f) << "an unquantized float must be bit-exact";
    EXPECT_EQ(wide, 3.14159265358979);
}

// A writer that runs out of room must say so rather than wrap or scribble.
TEST(NetBitStream, RefusesToOverrunItsBuffer)
{
    std::array<std::byte, 2> scratch{};
    NetBitWriter writer(scratch);
    writer.WriteU32(0xFFFFFFFF);
    EXPECT_TRUE(writer.Overflowed());
    EXPECT_EQ(writer.BitsWritten(), 0u) << "a refused write must not advance";
}

// The mirror: a truncated message must fail the read, not return zeros as if
// they were data.
TEST(NetBitStream, ReadingPastTheEndFails)
{
    std::array<std::byte, 1> scratch{};
    NetBitWriter writer(scratch);
    writer.WriteBits(0xF, 4);

    NetBitReader reader(writer.Written());
    std::uint32_t value = 0;
    ASSERT_TRUE(reader.ReadBits(4, value));
    EXPECT_EQ(value, 0xFu);
    EXPECT_TRUE(reader.ReadBits(4, value)) << "the byte's remaining bits are real";
    EXPECT_FALSE(reader.ReadBits(1, value));
    EXPECT_TRUE(reader.Overflowed());
}

// A stale scratch buffer must not leak bits of the previous message into this
// one. Every byte the writer reports is a byte it wrote.
TEST(NetBitStream, DoesNotInheritBitsFromAReusedBuffer)
{
    std::array<std::byte, 8> scratch{};
    scratch.fill(std::byte{ 0xFF });

    NetBitWriter writer(scratch);
    writer.WriteBits(0, 4);
    writer.WriteBits(0, 4);

    NetBitReader reader(writer.Written());
    std::uint32_t value = 0xFFFFFFFF;
    ASSERT_TRUE(reader.ReadBits(8, value));
    EXPECT_EQ(value, 0u);
}

//=============================================================================
// Quantization
//=============================================================================

TEST(ReplicationQuantization, RoundTripsWithinItsResolution)
{
    const FieldQuantization range{ -10.0f, 10.0f, 12 };
    const float resolution = 20.0f / static_cast<float>((1u << 12) - 1u);

    for (float value : { -10.0f, -7.5f, -0.001f, 0.0f, 1.0f, 3.33333f, 9.999f, 10.0f })
    {
        const float back =
            ReplicationDequantize(ReplicationQuantize(value, range), range);
        EXPECT_NEAR(back, value, resolution) << "value " << value;
    }
}

TEST(ReplicationQuantization, EndpointsAreExact)
{
    const FieldQuantization range{ -1.5707964f, 1.5707964f, 16 };
    EXPECT_FLOAT_EQ(ReplicationDequantize(ReplicationQuantize(range.Min, range), range),
                    range.Min);
    EXPECT_FLOAT_EQ(ReplicationDequantize(ReplicationQuantize(range.Max, range), range),
                    range.Max);
}

// Out of range clamps. A wrapped value would place an entity somewhere it has
// never been; a clamped one pins it visibly at the declared edge.
TEST(ReplicationQuantization, OutOfRangeClampsRatherThanWraps)
{
    const FieldQuantization range{ -1.0f, 1.0f, 8 };
    EXPECT_FLOAT_EQ(ReplicationDequantize(ReplicationQuantize(-500.0f, range), range),
                    -1.0f);
    EXPECT_FLOAT_EQ(ReplicationDequantize(ReplicationQuantize(500.0f, range), range),
                    1.0f);
}

// NaN and infinity fail every comparison, so they get their own coverage: a
// float from a hostile or broken peer must land somewhere in range.
TEST(ReplicationQuantization, HandlesNonFiniteInput)
{
    const FieldQuantization range{ -1.0f, 1.0f, 8 };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (float value : { nan, inf, -inf })
    {
        const float back =
            ReplicationDequantize(ReplicationQuantize(value, range), range);
        EXPECT_GE(back, range.Min);
        EXPECT_LE(back, range.Max);
        EXPECT_FALSE(std::isnan(back));
    }
}

// Snapping is what makes the authority's baseline equal to what a client holds,
// so a second encode against that baseline finds nothing changed.
TEST(ReplicationQuantization, SnappingIsIdempotent)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    LookOrientation value{ .Yaw = 1.234f, .Pitch = 0.4567f };
    ReplicationSnapToWire(look, MutableBytesOf(value));
    const LookOrientation once = value;

    ReplicationSnapToWire(look, MutableBytesOf(value));
    EXPECT_EQ(std::memcmp(&once, &value, sizeof(value)), 0)
        << "snapping an already-snapped value must not move it";
}

//=============================================================================
// Component encode and decode
//=============================================================================

TEST(ReplicationCodec, FullStateRoundTripsWithinQuantization)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    const LookOrientation sent{ .Yaw = 2.5f, .Pitch = -0.75f,
                                .MinPitch = -1.4f, .MaxPitch = 1.4f };
    LookOrientation received{};
    RoundTrip(look, BytesOf(sent), {}, MutableBytesOf(received), false);

    EXPECT_FLOAT_EQ(received.Yaw, sent.Yaw) << "yaw is unquantized, so it is exact";
    EXPECT_NEAR(received.Pitch, sent.Pitch, 0.001f);
}

// The fields the schema keeps local must be untouched by a decode, even when
// the sender's values differ. This is the exclusion holding end to end.
TEST(ReplicationCodec, LocalFieldsAreNotOverwrittenByADecode)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    const LookOrientation sent{ .Yaw = 1.0f, .Pitch = 0.0f,
                                .MinPitch = -9.0f, .MaxPitch = 9.0f };
    LookOrientation received{ .Yaw = 0.0f, .Pitch = 0.0f,
                              .MinPitch = -1.4f, .MaxPitch = 1.4f };
    RoundTrip(look, BytesOf(sent), {}, MutableBytesOf(received), false);

    EXPECT_FLOAT_EQ(received.Yaw, 1.0f);
    EXPECT_FLOAT_EQ(received.MinPitch, -1.4f)
        << "a local field must keep the receiver's own value";
    EXPECT_FLOAT_EQ(received.MaxPitch, 1.4f);
}

// The point of a delta: an unchanged component costs its mask and nothing else.
TEST(ReplicationCodec, AnUnchangedComponentCostsOnlyItsMask)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    LookOrientation state{ .Yaw = 1.25f, .Pitch = 0.5f };
    ReplicationSnapToWire(look, MutableBytesOf(state));

    LookOrientation received = state;
    const std::size_t bits =
        RoundTrip(look, BytesOf(state), BytesOf(state), MutableBytesOf(received));

    EXPECT_EQ(bits, look.Fields.size())
        << "an unchanged component should write its mask and no field bits";
    EXPECT_EQ(std::memcmp(&state, &received, sizeof(state)), 0);
}

TEST(ReplicationCodec, ADeltaCarriesOnlyTheChangedField)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    LookOrientation baseline{ .Yaw = 1.0f, .Pitch = 0.25f };
    ReplicationSnapToWire(look, MutableBytesOf(baseline));

    LookOrientation current = baseline;
    current.Yaw = 2.0f;  // pitch deliberately untouched

    LookOrientation received = baseline;
    const std::size_t bits = RoundTrip(look, BytesOf(current), BytesOf(baseline),
                                       MutableBytesOf(received), false);

    // Mask plus one 32-bit float, and specifically not the 16-bit pitch too.
    EXPECT_EQ(bits, look.Fields.size() + 32);
    EXPECT_FLOAT_EQ(received.Yaw, 2.0f);
    EXPECT_FLOAT_EQ(received.Pitch, baseline.Pitch);
}

// Jitter finer than the declared resolution is not a change: representing it
// would cost a field every tick to tell the receiver something it cannot hold.
TEST(ReplicationCodec, MovementBelowQuantizationResolutionIsNotAChange)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    LookOrientation baseline{ .Yaw = 1.0f, .Pitch = 0.5f };
    ReplicationSnapToWire(look, MutableBytesOf(baseline));

    LookOrientation current = baseline;
    current.Pitch += 1e-7f;  // far below 16-bit resolution over [-pi/2, pi/2]

    LookOrientation received = baseline;
    const std::size_t bits =
        RoundTrip(look, BytesOf(current), BytesOf(baseline), MutableBytesOf(received));

    EXPECT_EQ(bits, look.Fields.size()) << "sub-resolution jitter must not be sent";
}

// Applying a delta to a receiver that already agrees leaves it byte-identical
// to the authority's own snapped state. This is the property the desync hash
// will rest on.
TEST(ReplicationCodec, DeltaApplicationConvergesOnTheAuthorityState)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    LookOrientation authority{ .Yaw = 0.0f, .Pitch = 0.0f };
    LookOrientation client{};

    for (int tick = 1; tick <= 40; ++tick)
    {
        LookOrientation baseline = authority;

        authority.Yaw += 0.137f * static_cast<float>(tick % 5);
        authority.Pitch = std::sin(static_cast<float>(tick) * 0.2f);
        ReplicationSnapToWire(look, MutableBytesOf(authority));

        RoundTrip(look, BytesOf(authority), BytesOf(baseline), MutableBytesOf(client),
                  false);

        ASSERT_FLOAT_EQ(client.Yaw, authority.Yaw) << "tick " << tick;
        ASSERT_FLOAT_EQ(client.Pitch, authority.Pitch) << "tick " << tick;
    }
}

TEST(ReplicationCodec, OwnerOnlyFieldsAreWithheldFromOtherPeers)
{
    // Built here rather than from the engine table because no engine component
    // has an owner-only field yet; the mechanism still has to be proven.
    ReplicatedComponent component;
    component.Name = "test.Owned";
    component.Size = 2 * sizeof(float);
    component.Fields.push_back(ReplicatedField{
        .Name = "public", .Offset = 0, .Size = sizeof(float), .Count = 1,
        .Scalar = FieldScalar::Float });
    component.Fields.push_back(ReplicatedField{
        .Name = "private", .Offset = sizeof(float), .Size = sizeof(float), .Count = 1,
        .Scalar = FieldScalar::Float, .OwnerOnly = true });

    struct Owned { float Public; float Private; };
    const Owned sent{ 1.0f, 99.0f };

    Owned toOwner{ 0.0f, 0.0f };
    const std::size_t ownerBits =
        RoundTrip(component, BytesOf(sent), {}, MutableBytesOf(toOwner), true);
    EXPECT_FLOAT_EQ(toOwner.Private, 99.0f);
    EXPECT_EQ(ownerBits, 2u + 64u);

    Owned toOther{ 0.0f, -1.0f };
    const std::size_t otherBits =
        RoundTrip(component, BytesOf(sent), {}, MutableBytesOf(toOther), false);
    EXPECT_FLOAT_EQ(toOther.Public, 1.0f);
    EXPECT_FLOAT_EQ(toOther.Private, -1.0f)
        << "an owner-only field must not reach a peer that does not own the entity";
    EXPECT_EQ(otherBits, 2u + 32u) << "and must not cost bits either";
}

// A player's view has to follow their mouse now, not at the end of a round
// trip, so the authority never sends an owner its own aim -- the owner's local
// answer is always fresher than the echo.
TEST(ReplicationCodec, TheOwnerIsNotSentItsOwnAim)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    const LookOrientation authority{ .Yaw = 9.0f, .Pitch = 1.0f };
    LookOrientation owner{ .Yaw = 1.5f, .Pitch = -0.5f };
    const LookOrientation before = owner;
    const std::size_t ownerBits =
        RoundTrip(look, BytesOf(authority), {}, MutableBytesOf(owner), true);

    EXPECT_FLOAT_EQ(owner.Yaw, before.Yaw)
        << "the owner's own aim must not be overwritten by the echo";
    EXPECT_FLOAT_EQ(owner.Pitch, before.Pitch);
    EXPECT_EQ(ownerBits, look.Fields.size()) << "and it must not cost bits";

    LookOrientation spectator{};
    RoundTrip(look, BytesOf(authority), {}, MutableBytesOf(spectator), false);
    EXPECT_FLOAT_EQ(spectator.Yaw, 9.0f)
        << "everyone else still has to see where the player is aiming";
}

//=============================================================================
// Hostile input
//
// The decode path is reachable by anything a peer sends, including a
// compromised authority sending to a client.
//=============================================================================

TEST(ReplicationCodecHostile, TruncatedMessagesAreRefused)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    const LookOrientation sent{ .Yaw = 2.5f, .Pitch = -0.75f };
    std::array<std::byte, kScratchBytes> scratch{};
    NetBitWriter writer(scratch);
    ASSERT_TRUE(ReplicationEncodeComponent(look, BytesOf(sent), {}, true, writer));

    // Every proper prefix of a valid message must be refused.
    const std::span<const std::byte> whole = writer.Written();
    for (std::size_t length = 0; length < whole.size(); ++length)
    {
        LookOrientation target{};
        NetBitReader reader(whole.subspan(0, length));
        EXPECT_FALSE(ReplicationDecodeComponent(look, reader, MutableBytesOf(target)))
            << "accepted a message truncated to " << length << " bytes";
    }
}

TEST(ReplicationCodecHostile, AWrongSizedTargetIsRefused)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    std::array<std::byte, 4> tooSmall{};
    NetBitReader reader(tooSmall);
    EXPECT_FALSE(ReplicationDecodeComponent(look, reader, tooSmall));
}

// Arbitrary bytes must decode to something in range or fail, never to a NaN
// position or an out-of-range angle that then propagates into the simulation.
TEST(ReplicationCodecHostile, ArbitraryBytesDecodeInRangeOrFail)
{
    const ReplicationLayout layout = EngineLayout();
    const ReplicatedComponent& look = LookComponent(layout);

    std::uint32_t seed = 0x1234567u;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::byte>((seed >> 16) & 0xFF);
    };

    for (int trial = 0; trial < 4000; ++trial)
    {
        std::array<std::byte, 16> noise{};
        for (std::byte& byte : noise)
            byte = next();

        LookOrientation target{};
        NetBitReader reader(noise);
        if (!ReplicationDecodeComponent(look, reader, MutableBytesOf(target)))
            continue;

        // Pitch is quantized, so whatever the bits said it lands in its range.
        EXPECT_GE(target.Pitch, -1.5707965f) << "trial " << trial;
        EXPECT_LE(target.Pitch, 1.5707965f) << "trial " << trial;
        EXPECT_FALSE(std::isnan(target.Pitch)) << "trial " << trial;
    }
}

//=============================================================================
// The engine table
//=============================================================================

TEST(ReplicationCodec, EveryEngineComponentRoundTripsFromDefaults)
{
    const ReplicationLayout layout = EngineLayout();

    // Both audiences, because ownership decides which fields travel: a field
    // withheld from this receiver is the codec doing its job, not a loss.
    for (const bool forOwner : { false, true })
    {
        for (const ReplicatedComponent& component : layout.Components())
        {
            std::vector<std::byte> current(component.Size, std::byte{ 0 });
            std::vector<std::byte> target(component.Size, std::byte{ 0 });

            // A recognisable pattern, snapped to what the wire can carry so the
            // comparison below is exact rather than approximate.
            for (std::size_t i = 0; i < current.size(); ++i)
                current[i] = static_cast<std::byte>((i * 7 + 3) & 0x3F);
            ReplicationSnapToWire(component, current);

            std::array<std::byte, kScratchBytes> scratch{};
            NetBitWriter writer(scratch);
            ASSERT_TRUE(
                ReplicationEncodeComponent(component, current, {}, forOwner, writer))
                << component.Name;
            ASSERT_LE(writer.BitsWritten(), ReplicationMaxComponentBits(component))
                << component.Name << " exceeded its own stated maximum";

            NetBitReader reader(writer.Written());
            ASSERT_TRUE(ReplicationDecodeComponent(component, reader, target))
                << component.Name;

            // Only fields addressed to this receiver are claimed to survive;
            // the rest of the component is the receiver's own business.
            for (const ReplicatedField& field : component.Fields)
            {
                if (field.OwnerOnly && !forOwner)
                    continue;
                if (field.OwnerLocal && forOwner)
                    continue;
                const std::size_t span = field.Count * field.Size;
                EXPECT_EQ(std::memcmp(current.data() + field.Offset,
                                      target.data() + field.Offset, span), 0)
                    << component.Name << "." << field.Name
                    << (forOwner ? " (as owner)" : " (as peer)");
            }
        }
    }
}
