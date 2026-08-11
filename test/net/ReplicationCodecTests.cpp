#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationSchemas.h>
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
        // What a difference would carry, narrowed to what this receiver may
        // see -- the two questions the writer composes.
        const std::uint64_t fields =
            ReplicationChangedFields(component, current, baseline)
            & ReplicationVisibleFields(component, forOwner);
        EXPECT_TRUE(ReplicationEncodeComponent(component, current, fields, writer));
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
    ASSERT_TRUE(ReplicationEncodeComponent(
        look, BytesOf(sent), ReplicationVisibleFields(look, true), writer));

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
            ASSERT_TRUE(ReplicationEncodeComponent(
                component, current, ReplicationVisibleFields(component, forOwner),
                writer))
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

//=============================================================================
// Variable-width identities
//
// An entity identity is minted from one and never reused, so most of a
// session's identities are small and none of them has a ceiling. A fixed
// sixty-four bits was most of what an entity's envelope cost, paid on every
// entity in every snapshot to every peer.
//=============================================================================

TEST(NetBitVarUInt, RoundTripsAcrossEveryWidth)
{
    std::array<std::byte, 32> buffer{};
    for (std::uint64_t value : { std::uint64_t{ 0 }, std::uint64_t{ 1 },
                                 std::uint64_t{ 127 }, std::uint64_t{ 128 },
                                 std::uint64_t{ 16383 }, std::uint64_t{ 16384 },
                                 std::uint64_t{ 1ull << 32 },
                                 ~std::uint64_t{ 0 } })
    {
        NetBitWriter writer(buffer);
        writer.WriteVarUInt(value);
        ASSERT_FALSE(writer.Overflowed()) << value;

        NetBitReader reader(writer.Written());
        std::uint64_t read = 0;
        ASSERT_TRUE(reader.ReadVarUInt(read)) << value;
        EXPECT_EQ(read, value);
    }
}

// The saving is the whole point, so it is asserted rather than assumed.
TEST(NetBitVarUInt, SmallIdentitiesCostAByte)
{
    const auto bits = [](std::uint64_t value) {
        std::array<std::byte, 32> buffer{};
        NetBitWriter writer(buffer);
        writer.WriteVarUInt(value);
        return writer.BitsWritten();
    };

    EXPECT_EQ(bits(1), 8u);
    EXPECT_EQ(bits(127), 8u);
    EXPECT_EQ(bits(128), 16u);
    EXPECT_EQ(bits(16383), 16u);
    EXPECT_EQ(bits(16384), 24u);
    // The worst case is still bounded, and it is what a fixed-width identity
    // cost every time rather than only past eighteen quintillion.
    EXPECT_EQ(bits(~std::uint64_t{ 0 }), 80u);
}

// Continuation bits come from a peer. A reader that kept going while they said
// to would be letting the sender decide how long the read runs.
TEST(NetBitVarUInt, AnEndlessRunOfContinuationsIsRefused)
{
    std::array<std::byte, 32> buffer{};
    NetBitWriter writer(buffer);
    for (int group = 0; group < 20; ++group)
    {
        writer.WriteBits(0x7F, 7);
        writer.WriteBool(true);  // and another follows, forever
    }

    NetBitReader reader(writer.Written());
    std::uint64_t value = 0;
    EXPECT_FALSE(reader.ReadVarUInt(value));
    EXPECT_TRUE(reader.Overflowed());
}

//=============================================================================
// A transform at wire precision
//
// The precision a link carries a value at is a fact about the link, not about
// the type. That is why it is stated by the layer doing the sending: the scene
// format, the inspector, and the physics step all keep reading a transform at
// full width, and none of them is affected by what a snapshot spends on one.
//=============================================================================

namespace
{
    // The step between two values the wire can name.
    constexpr float WireStep(float range, std::uint8_t bits)
    {
        return 2.0f * range / static_cast<float>((std::uint32_t{ 1 } << bits) - 1u);
    }

    const ReplicatedComponent& TransformOnTheWire(ReplicationLayout& layout)
    {
        EXPECT_TRUE(layout.Add<LocalTransform>());
        const ReplicatedComponent* component =
            layout.Find(ResolveComponentTypeId<LocalTransform>());
        EXPECT_NE(component, nullptr);
        return *component;
    }

    // Encodes then decodes a whole transform, so what comes back is what a peer
    // would actually be holding.
    LocalTransform ThroughTheWire(const ReplicatedComponent& component,
                                  const LocalTransform& sent)
    {
        std::array<std::byte, kScratchBytes> buffer{};
        NetBitWriter writer(buffer);
        const std::uint64_t all = ~std::uint64_t{ 0 };
        EXPECT_TRUE(ReplicationEncodeComponent(component, BytesOf(sent), all, writer));

        LocalTransform received{};
        NetBitReader reader(writer.Written());
        EXPECT_TRUE(ReplicationDecodeComponent(
            component, reader,
            std::span(reinterpret_cast<std::byte*>(&received), sizeof(received))));
        return received;
    }
}

TEST(ReplicationTransformPrecision, EveryPartArrivesWithinItsDeclaredStep)
{
    ReplicationLayout layout;
    const ReplicatedComponent& component = TransformOnTheWire(layout);

    LocalTransform sent{};
    sent.Value.Position = Vec3d{ 12.3456f, -700.125f, 0.0009f };
    sent.Value.Rotation = Quatf{ 0.5f, -0.5f, 0.5f, 0.5f };
    sent.Value.Scale = Vec3d{ 1.0f, 2.5f, -0.75f };

    const LocalTransform got = ThroughTheWire(component, sent);

    const float position = WireStep(kNetPositionRange, kNetPositionBits);
    EXPECT_NEAR(got.Value.Position.X, sent.Value.Position.X, position);
    EXPECT_NEAR(got.Value.Position.Y, sent.Value.Position.Y, position);
    EXPECT_NEAR(got.Value.Position.Z, sent.Value.Position.Z, position);

    const float rotation = WireStep(kNetRotationRange, kNetRotationBits);
    EXPECT_NEAR(got.Value.Rotation.X, sent.Value.Rotation.X, rotation);
    EXPECT_NEAR(got.Value.Rotation.Y, sent.Value.Rotation.Y, rotation);
    EXPECT_NEAR(got.Value.Rotation.Z, sent.Value.Rotation.Z, rotation);
    EXPECT_NEAR(got.Value.Rotation.W, sent.Value.Rotation.W, rotation);

    const float scale = WireStep(kNetScaleRange, kNetScaleBits);
    EXPECT_NEAR(got.Value.Scale.X, sent.Value.Scale.X, scale);
    EXPECT_NEAR(got.Value.Scale.Y, sent.Value.Scale.Y, scale);
    EXPECT_NEAR(got.Value.Scale.Z, sent.Value.Scale.Z, scale);
}

// A scale of exactly one is what nearly every entity has, and it has to survive
// the trip: a range too coarse to name it would make every replicated entity
// permanently the wrong size, everywhere, for no reason a player could trace.
TEST(ReplicationTransformPrecision, TheScaleEverythingHasSurvivesIntact)
{
    ReplicationLayout layout;
    const ReplicatedComponent& component = TransformOnTheWire(layout);

    LocalTransform sent{};
    sent.Value.Scale = Vec3d{ 1.0f, 1.0f, 1.0f };
    const LocalTransform got = ThroughTheWire(component, sent);

    // A tenth of a millimetre per unit. Anything coarser is visible on a
    // character-sized object.
    EXPECT_NEAR(got.Value.Scale.X, 1.0f, 1e-3f);
    EXPECT_NEAR(got.Value.Scale.Y, 1.0f, 1e-3f);
    EXPECT_NEAR(got.Value.Scale.Z, 1.0f, 1e-3f);
}

// The range is a declaration about how large a world can be. Beyond it a
// position pins to the edge rather than wrapping -- an entity in the wrong
// place, which is bad, rather than an entity somewhere it has never been, which
// is worse and untraceable.
TEST(ReplicationTransformPrecision, APositionBeyondTheDeclaredWorldPinsToItsEdge)
{
    ReplicationLayout layout;
    const ReplicatedComponent& component = TransformOnTheWire(layout);

    LocalTransform sent{};
    sent.Value.Position = Vec3d{ kNetPositionRange * 4.0f, -kNetPositionRange * 4.0f,
                                 0.0f };
    const LocalTransform got = ThroughTheWire(component, sent);

    EXPECT_NEAR(got.Value.Position.X, kNetPositionRange,
                WireStep(kNetPositionRange, kNetPositionBits));
    EXPECT_NEAR(got.Value.Position.Y, -kNetPositionRange,
                WireStep(kNetPositionRange, kNetPositionBits));
}

// The layering claim of the whole arrangement. If a wire decision reached the
// authoring description, a scene would round-trip through it and a saved level
// would quietly lose precision it was authored with.
TEST(ReplicationTransformPrecision, TheAuthoredDescriptionIsUntouched)
{
    for (const RuntimeField& field : RuntimeFieldsOf<LocalTransform>())
    {
        EXPECT_FALSE(field.Quantization.IsQuantized())
            << field.Name << " is described to authoring at wire precision";
    }

    // And the replication description of the same type is not.
    bool anyQuantized = false;
    for (const RuntimeField& field :
         RuntimeFieldsOf<LocalTransform, SchemaPurpose::Replication>())
    {
        anyQuantized = anyQuantized || field.Quantization.IsQuantized();
    }
    EXPECT_TRUE(anyQuantized)
        << "nothing is quantized, so the two descriptions are the same one";
}

//=============================================================================
// Narrow integers
//
// A field's scalar category says how the value travels; its Size says how many
// bytes of the component it occupies. Every integral narrower than four bytes
// travels in the 32-bit category, so those two numbers routinely disagree, and
// an access that takes its width from the category reaches past the field into
// whatever the component keeps next to it.
//
// These fixtures put something there worth noticing.
//=============================================================================

namespace
{
    enum class Slot : std::uint8_t
    {
        None,
        Primary,
        Secondary,
    };

    // Every narrow shape, each with a neighbour immediately after it. Laid out
    // so there is no padding to absorb an over-wide access: 2+2+1+1+2 with
    // two-byte alignment is exactly eight bytes.
    struct NarrowNeighbours
    {
        std::uint16_t Small = 0;
        std::uint16_t Canary = 0;
        std::uint8_t Tiny = 0;
        Slot Kind = Slot::None;
        std::int16_t Signed = 0;
    };

    // Nothing but the narrow field, so the field's bytes are the whole object
    // and an over-wide access has nothing of its own to land in.
    struct NarrowAlone
    {
        std::uint16_t Only = 0;
    };
}

template <>
struct TypeSchema<NarrowNeighbours>
{
    static constexpr std::string_view Name = "test.NarrowNeighbours";
    static auto Fields()
    {
        return std::tuple{
            MakeField("small", &NarrowNeighbours::Small),
            MakeField("canary", &NarrowNeighbours::Canary),
            MakeField("tiny", &NarrowNeighbours::Tiny),
            MakeField("kind", &NarrowNeighbours::Kind),
            MakeField("signed", &NarrowNeighbours::Signed),
        };
    }
};

template <>
struct TypeSchema<NarrowAlone>
{
    static constexpr std::string_view Name = "test.NarrowAlone";
    static auto Fields()
    {
        return std::tuple{ MakeField("only", &NarrowAlone::Only) };
    }
};

namespace
{
    const ReplicatedComponent& NarrowComponent(const ReplicationLayout& layout)
    {
        const ReplicatedComponent* component =
            layout.Find(ResolveComponentTypeId<NarrowNeighbours>());
        EXPECT_NE(component, nullptr);
        return *component;
    }

    ReplicationLayout NarrowLayout()
    {
        ReplicationLayout layout;
        EXPECT_TRUE(layout.Add<NarrowNeighbours>());
        EXPECT_TRUE(layout.Add<NarrowAlone>());
        layout.Seal();
        EXPECT_EQ(layout.Error(), ReplicationLayoutError::None)
            << layout.ErrorDetail();
        return layout;
    }
}

// The delta contract, at the one place it is easiest to break: a field whose
// mask bit is clear is left exactly as it was. A store that takes its width
// from the category writes four bytes at a two-byte field and takes the
// neighbour with it, which turns every narrow field into a silent corrupter of
// whatever the component declared next.
TEST(ReplicationNarrowScalars, DecodingOneFieldLeavesItsNeighbourAlone)
{
    const ReplicationLayout layout = NarrowLayout();
    const ReplicatedComponent& component = NarrowComponent(layout);

    NarrowNeighbours source{};
    source.Small = 0x1234;
    source.Canary = 0xBEEF;

    NarrowNeighbours baseline = source;
    baseline.Small = 0x1111;  // the only run the encoder will find changed

    std::array<std::byte, kScratchBytes> scratch{};
    NetBitWriter writer(scratch);
    const std::uint64_t changed =
        ReplicationChangedFields(component, BytesOf(source), BytesOf(baseline));
    ASSERT_TRUE(ReplicationEncodeComponent(component, BytesOf(source), changed, writer));

    NarrowNeighbours target{};
    target.Canary = 0xC0DE;  // what the receiver already holds
    NetBitReader reader(writer.Written());
    ASSERT_TRUE(ReplicationDecodeComponent(component, reader, MutableBytesOf(target)));

    EXPECT_EQ(target.Small, 0x1234);
    EXPECT_EQ(target.Canary, 0xC0DE)
        << "a field the delta did not carry was overwritten anyway";
}

// The other direction, and the reason OwnerOnly is not merely a visibility
// nicety: a load that reads four bytes at a two-byte field puts the next field
// on the wire inside the same word, whatever the mask said about it. Two
// encodings that differ only in a field nobody asked for must be identical.
TEST(ReplicationNarrowScalars, EncodingOneFieldCarriesOnlyItsOwnBytes)
{
    const ReplicationLayout layout = NarrowLayout();
    const ReplicatedComponent& component = NarrowComponent(layout);

    const std::uint64_t onlySmall = 1;  // field zero, and nothing else

    NarrowNeighbours first{};
    first.Small = 0x1234;
    first.Canary = 0x0000;

    NarrowNeighbours second{};
    second.Small = 0x1234;
    second.Canary = 0xFFFF;

    std::array<std::byte, kScratchBytes> a{};
    std::array<std::byte, kScratchBytes> b{};
    NetBitWriter writerA(a);
    NetBitWriter writerB(b);
    ASSERT_TRUE(ReplicationEncodeComponent(component, BytesOf(first), onlySmall, writerA));
    ASSERT_TRUE(ReplicationEncodeComponent(component, BytesOf(second), onlySmall, writerB));

    ASSERT_EQ(writerA.BitsWritten(), writerB.BitsWritten());
    EXPECT_TRUE(std::equal(writerA.Written().begin(), writerA.Written().end(),
                           writerB.Written().begin()))
        << "a field outside the mask reached the wire inside its neighbour's word";
}

TEST(ReplicationNarrowScalars, EveryNarrowWidthRoundTripsItsFullRange)
{
    const ReplicationLayout layout = NarrowLayout();
    const ReplicatedComponent& component = NarrowComponent(layout);

    NarrowNeighbours source{};
    source.Small = 0xFFFF;
    source.Canary = 0xABCD;
    source.Tiny = 0xFF;
    source.Kind = Slot::Secondary;
    source.Signed = -32768;

    NarrowNeighbours target{};
    RoundTrip(component, BytesOf(source), {}, MutableBytesOf(target));

    EXPECT_EQ(target.Small, 0xFFFF);
    EXPECT_EQ(target.Canary, 0xABCD);
    EXPECT_EQ(target.Tiny, 0xFF);
    EXPECT_EQ(target.Kind, Slot::Secondary);
    EXPECT_EQ(target.Signed, -32768) << "a signed narrow field lost its sign";
}

// The same access, with nothing of the component's own on either side of it.
// Heap-allocated and sized exactly, so the bytes past the field belong to the
// allocator rather than to the component -- which is what makes this the case a
// sanitizer can see and a value comparison cannot.
TEST(ReplicationNarrowScalars, ANarrowFieldTouchesNothingOutsideItsComponent)
{
    const ReplicationLayout layout = NarrowLayout();
    const ReplicatedComponent* component =
        layout.Find(ResolveComponentTypeId<NarrowAlone>());
    ASSERT_NE(component, nullptr);
    ASSERT_EQ(component->Size, sizeof(NarrowAlone));

    std::vector<std::byte> source(component->Size);
    const std::uint16_t value = 0x7A7A;
    std::memcpy(source.data(), &value, sizeof(value));

    std::array<std::byte, kScratchBytes> scratch{};
    NetBitWriter writer(scratch);
    ASSERT_TRUE(ReplicationEncodeComponent(*component, source, ~std::uint64_t{ 0 }, writer));

    std::vector<std::byte> target(component->Size);
    NetBitReader reader(writer.Written());
    ASSERT_TRUE(ReplicationDecodeComponent(*component, reader, target));

    std::uint16_t decoded = 0;
    std::memcpy(&decoded, target.data(), sizeof(decoded));
    EXPECT_EQ(decoded, value);
}

// The fill arithmetic and the encoder are separate code reading the same
// layout, which is the shape that drifts silently. An entity is admitted to a
// snapshot on the sizer's answer and then written with the encoder's, so any
// field where they disagree is either wasted budget or -- at the moment the
// budget is full -- a snapshot that overflows and a peer that receives nothing.
TEST(ReplicationCodec, WhatTheSizerReservesIsWhatTheWriterSpends)
{
    const ReplicationLayout layout = EngineLayout();

    for (const bool forOwner : { false, true })
    {
        for (const ReplicatedComponent& component : layout.Components())
        {
            std::vector<std::byte> current(component.Size, std::byte{ 0 });
            for (std::size_t i = 0; i < current.size(); ++i)
                current[i] = static_cast<std::byte>((i * 11 + 5) & 0x3F);
            ReplicationSnapToWire(component, current);

            const std::uint64_t visible =
                ReplicationVisibleFields(component, forOwner);

            // Every subset that matters: nothing owed, everything owed, and
            // each single field on its own -- a per-field disagreement hides
            // inside a full mask when another field happens to cancel it.
            std::vector<std::uint64_t> masks{ 0, visible };
            for (std::size_t i = 0; i < component.Fields.size(); ++i)
                masks.push_back(visible & (std::uint64_t{ 1 } << i));

            for (const std::uint64_t mask : masks)
            {
                std::array<std::byte, kScratchBytes> scratch{};
                NetBitWriter writer(scratch);
                ASSERT_TRUE(ReplicationEncodeComponent(component, current, mask, writer))
                    << component.Name;
                EXPECT_EQ(writer.BitsWritten(),
                          ReplicationEncodedComponentBits(component, mask))
                    << component.Name << " measured differently than it was written";
            }
        }
    }
}
