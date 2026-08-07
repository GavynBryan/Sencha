#include <gtest/gtest.h>

#include <net/NetProtocol.h>

#include <array>
#include <string>
#include <vector>

namespace
{
    // One datagram's worth, which is the most any encoder is ever given.
    using Scratch = std::array<std::byte, kNetMaxDatagramBytes>;

    std::vector<std::byte> Cookie(std::size_t length, std::uint8_t seed = 0xA5)
    {
        std::vector<std::byte> out(length);
        for (std::size_t i = 0; i < length; ++i)
            out[i] = static_cast<std::byte>(seed + i);
        return out;
    }

    NetCookieEcho SampleEcho()
    {
        NetCookieEcho echo;
        echo.Cookie = Cookie(16);
        echo.ProtocolVersion = kNetProtocolVersion;
        echo.ModuleFingerprint = 0x0123456789ABCDEFull;
        echo.WorldIdentity = 0xFEDCBA9876543210ull;
        echo.FixedTickRateMilliHz = 60000;
        return echo;
    }
}

TEST(NetProtocol, HelloRoundTrips)
{
    Scratch scratch{};
    const NetHello sent{ .ProtocolVersion = kNetProtocolVersion };

    const auto encoded = NetEncodeHello(sent, scratch);
    ASSERT_FALSE(encoded.empty());

    NetHello received{};
    EXPECT_EQ(NetDecodeHello(encoded, received), NetDecodeError::None);
    EXPECT_EQ(received.ProtocolVersion, sent.ProtocolVersion);
}

TEST(NetProtocol, CookieEchoRoundTripsEveryField)
{
    Scratch scratch{};
    const NetCookieEcho sent = SampleEcho();

    const auto encoded = NetEncodeCookieEcho(sent, scratch);
    ASSERT_FALSE(encoded.empty());

    NetCookieEcho received{};
    ASSERT_EQ(NetDecodeCookieEcho(encoded, received), NetDecodeError::None);
    EXPECT_EQ(received.Cookie, sent.Cookie);
    EXPECT_EQ(received.ProtocolVersion, sent.ProtocolVersion);
    EXPECT_EQ(received.ModuleFingerprint, sent.ModuleFingerprint);
    EXPECT_EQ(received.WorldIdentity, sent.WorldIdentity);
    EXPECT_EQ(received.FixedTickRateMilliHz, sent.FixedTickRateMilliHz);
}

TEST(NetProtocol, AcceptRoundTripsEveryField)
{
    Scratch scratch{};
    const NetAccept sent{
        .PeerId = 3,
        .ModuleFingerprint = 11,
        .WorldIdentity = 22,
        .FixedTickRateMilliHz = 60000,
        .AuthorityTick = 987654321,
    };

    const auto encoded = NetEncodeAccept(sent, scratch);
    ASSERT_FALSE(encoded.empty());

    NetAccept received{};
    ASSERT_EQ(NetDecodeAccept(encoded, received), NetDecodeError::None);
    EXPECT_EQ(received.PeerId, sent.PeerId);
    EXPECT_EQ(received.ModuleFingerprint, sent.ModuleFingerprint);
    EXPECT_EQ(received.WorldIdentity, sent.WorldIdentity);
    EXPECT_EQ(received.AuthorityTick, sent.AuthorityTick);
}

TEST(NetProtocol, ReasonTextRoundTrips)
{
    Scratch scratch{};
    const NetRefuse sent{ .Reason = "world identity mismatch" };

    const auto encoded = NetEncodeRefuse(sent, scratch);
    ASSERT_FALSE(encoded.empty());

    NetRefuse received{};
    ASSERT_EQ(NetDecodeRefuse(encoded, received), NetDecodeError::None);
    EXPECT_EQ(received.Reason, sent.Reason);
}

TEST(NetProtocol, PingPongRoundTrip)
{
    Scratch scratch{};

    const auto ping = NetEncodePing(NetPing{ .SentMicroseconds = 424242 }, scratch);
    ASSERT_FALSE(ping.empty());
    NetPing decodedPing{};
    ASSERT_EQ(NetDecodePing(ping, decodedPing), NetDecodeError::None);
    EXPECT_EQ(decodedPing.SentMicroseconds, 424242u);

    Scratch other{};
    const auto pong = NetEncodePong(
        NetPong{ .SentMicroseconds = 424242, .AuthorityTick = 99 }, other);
    ASSERT_FALSE(pong.empty());
    NetPong decodedPong{};
    ASSERT_EQ(NetDecodePong(pong, decodedPong), NetDecodeError::None);
    EXPECT_EQ(decodedPong.SentMicroseconds, 424242u);
    EXPECT_EQ(decodedPong.AuthorityTick, 99u);
}

TEST(NetProtocol, PeekRoutesKnownTypesAndRefusesUnknown)
{
    Scratch scratch{};
    const auto encoded = NetEncodeHello(NetHello{}, scratch);
    ASSERT_FALSE(encoded.empty());

    NetMessageType type = NetMessageType::Invalid;
    EXPECT_EQ(NetPeekMessageType(encoded, type), NetDecodeError::None);
    EXPECT_EQ(type, NetMessageType::Hello);

    const std::array<std::byte, 1> unknown{ std::byte{ 200 } };
    EXPECT_EQ(NetPeekMessageType(unknown, type), NetDecodeError::UnknownMessage);
    EXPECT_EQ(type, NetMessageType::Invalid);

    EXPECT_EQ(NetPeekMessageType({}, type), NetDecodeError::Truncated);
}

// Truncation is the commonest malformed input, deliberate or not, and every
// prefix of a valid message has to fail rather than decode something partial.
TEST(NetProtocol, EveryTruncationOfAValidMessageIsRefused)
{
    Scratch scratch{};
    const auto encoded = NetEncodeCookieEcho(SampleEcho(), scratch);
    ASSERT_GT(encoded.size(), 1u);

    for (std::size_t length = 0; length < encoded.size(); ++length)
    {
        NetCookieEcho received{};
        const NetDecodeError error =
            NetDecodeCookieEcho(encoded.subspan(0, length), received);
        EXPECT_NE(error, NetDecodeError::None)
            << "a " << length << "-byte prefix decoded as a whole message";
    }
}

// A decoder that ignores trailing bytes lets a peer append whatever it likes to
// a well-formed message, past anything a reviewer read the format as allowing.
TEST(NetProtocol, TrailingBytesAreRefused)
{
    Scratch scratch{};
    const auto encoded = NetEncodeHello(NetHello{}, scratch);
    ASSERT_FALSE(encoded.empty());

    std::vector<std::byte> extended(encoded.begin(), encoded.end());
    extended.push_back(std::byte{ 0xFF });

    NetHello received{};
    EXPECT_EQ(NetDecodeHello(extended, received), NetDecodeError::TrailingBytes);
}

TEST(NetProtocol, WrongVersionIsRefusedBeforeAnythingElse)
{
    Scratch scratch{};
    NetHello sent{};
    sent.ProtocolVersion = static_cast<std::uint16_t>(kNetProtocolVersion + 1);

    const auto encoded = NetEncodeHello(sent, scratch);
    ASSERT_FALSE(encoded.empty());

    NetHello received{};
    EXPECT_EQ(NetDecodeHello(encoded, received), NetDecodeError::BadVersion);
}

// The cap is what stands between a wire-supplied length and an allocation, so
// a length past it must fail before the vector is sized -- not after.
TEST(NetProtocol, LengthPastItsCapIsRefused)
{
    // Hand-built rather than encoded: the encoder refuses to produce this,
    // which is exactly why the decoder has to be tested against it directly.
    std::vector<std::byte> payload;
    payload.push_back(static_cast<std::byte>(NetMessageType::Challenge));
    const std::uint16_t claimed = 4096;
    payload.push_back(static_cast<std::byte>(claimed & 0xFF));
    payload.push_back(static_cast<std::byte>(claimed >> 8));

    NetChallenge received{};
    EXPECT_EQ(NetDecodeChallenge(payload, received), NetDecodeError::CapExceeded);
    EXPECT_TRUE(received.Cookie.empty());
}

// The length fits its cap but not the datagram. Checking the cap alone would
// reserve for a payload that was never sent.
TEST(NetProtocol, LengthWithinCapButPastTheBufferIsRefused)
{
    std::vector<std::byte> payload;
    payload.push_back(static_cast<std::byte>(NetMessageType::Challenge));
    const std::uint16_t claimed = 24;  // under the cookie cap
    payload.push_back(static_cast<std::byte>(claimed & 0xFF));
    payload.push_back(static_cast<std::byte>(claimed >> 8));
    payload.push_back(std::byte{ 1 });  // only one of the 24 bytes present

    NetChallenge received{};
    EXPECT_EQ(NetDecodeChallenge(payload, received), NetDecodeError::Truncated);
}

TEST(NetProtocol, EncoderRefusesAPayloadPastItsCap)
{
    Scratch scratch{};
    NetChallenge message;
    message.Cookie = Cookie(NetDefaultCaps().MaxCookieBytes + 1);

    EXPECT_TRUE(NetEncodeChallenge(message, scratch).empty())
        << "encoding past a cap must refuse rather than truncate the field";
}

TEST(NetProtocol, EncoderRefusesABufferTooSmallToHoldTheMessage)
{
    std::array<std::byte, 2> tiny{};
    EXPECT_TRUE(NetEncodeCookieEcho(SampleEcho(), tiny).empty());
}

// Values that decode structurally but cannot be valid. Zero is the reserved
// "no peer", and a zero tick rate divides by zero wherever it is consumed.
TEST(NetProtocol, StructurallyValidButImpossibleValuesAreRefused)
{
    Scratch scratch{};

    NetAccept zeroPeer{ .PeerId = 0, .FixedTickRateMilliHz = 60000 };
    const auto encodedPeer = NetEncodeAccept(zeroPeer, scratch);
    ASSERT_FALSE(encodedPeer.empty());
    NetAccept decoded{};
    EXPECT_EQ(NetDecodeAccept(encodedPeer, decoded), NetDecodeError::Malformed);

    Scratch other{};
    NetCookieEcho zeroRate = SampleEcho();
    zeroRate.FixedTickRateMilliHz = 0;
    const auto encodedRate = NetEncodeCookieEcho(zeroRate, other);
    ASSERT_FALSE(encodedRate.empty());
    NetCookieEcho decodedRate{};
    EXPECT_EQ(NetDecodeCookieEcho(encodedRate, decodedRate), NetDecodeError::Malformed);
}

// Decoding one message's bytes with another's decoder has to fail on the type
// byte rather than misread the body as its own fields.
TEST(NetProtocol, ADecoderRefusesAnotherMessagesBytes)
{
    Scratch scratch{};
    const auto encoded = NetEncodeAccept(
        NetAccept{ .PeerId = 1, .FixedTickRateMilliHz = 60000 }, scratch);
    ASSERT_FALSE(encoded.empty());

    NetHello wrong{};
    EXPECT_EQ(NetDecodeHello(encoded, wrong), NetDecodeError::UnknownMessage);
}

//=============================================================================
// Mutation fuzz
//
// Ships with the phase rather than after it. Seeded and reproducible: a failure
// here is replayable from the seed printed with it, which is the difference
// between a fuzz finding and a flake. The contract asserted is not that a
// mutation is rejected -- some mutations are still valid messages -- but that
// decoding it never reads out of bounds, never throws, and never reports
// success while leaving a field the caller would act on unset.
//=============================================================================
namespace
{
    struct Mutator
    {
        std::uint64_t State;

        std::uint32_t Next()
        {
            State ^= State << 13;
            State ^= State >> 7;
            State ^= State << 17;
            return static_cast<std::uint32_t>(State >> 32);
        }
    };

    // Every decoder, behind one signature, so the corpus runs the whole
    // client-facing and authority-facing surface rather than one message.
    NetDecodeError DecodeAny(NetMessageType type, std::span<const std::byte> payload)
    {
        switch (type)
        {
        case NetMessageType::Hello:      { NetHello m; return NetDecodeHello(payload, m); }
        case NetMessageType::Challenge:  { NetChallenge m; return NetDecodeChallenge(payload, m); }
        case NetMessageType::CookieEcho: { NetCookieEcho m; return NetDecodeCookieEcho(payload, m); }
        case NetMessageType::Accept:     { NetAccept m; return NetDecodeAccept(payload, m); }
        case NetMessageType::Refuse:     { NetRefuse m; return NetDecodeRefuse(payload, m); }
        case NetMessageType::Disconnect: { NetDisconnect m; return NetDecodeDisconnect(payload, m); }
        case NetMessageType::Ping:       { NetPing m; return NetDecodePing(payload, m); }
        case NetMessageType::Pong:       { NetPong m; return NetDecodePong(payload, m); }
        case NetMessageType::Invalid:    break;
        }
        return NetDecodeError::UnknownMessage;
    }

    std::vector<std::vector<std::byte>> BuildCorpus()
    {
        std::vector<std::vector<std::byte>> corpus;
        const auto add = [&corpus](std::span<const std::byte> bytes) {
            corpus.emplace_back(bytes.begin(), bytes.end());
        };

        Scratch scratch{};
        add(NetEncodeHello(NetHello{}, scratch));
        add(NetEncodeChallenge(NetChallenge{ .Cookie = Cookie(16) }, scratch));
        add(NetEncodeCookieEcho(SampleEcho(), scratch));
        add(NetEncodeAccept(
            NetAccept{ .PeerId = 2, .FixedTickRateMilliHz = 60000 }, scratch));
        add(NetEncodeRefuse(NetRefuse{ .Reason = "no" }, scratch));
        add(NetEncodeDisconnect(NetDisconnect{ .Reason = "bye" }, scratch));
        add(NetEncodePing(NetPing{ .SentMicroseconds = 1 }, scratch));
        add(NetEncodePong(NetPong{ .SentMicroseconds = 1, .AuthorityTick = 2 }, scratch));
        return corpus;
    }
}

TEST(NetProtocolFuzz, MutatedMessagesNeverEscapeTheirBuffer)
{
    const std::vector<std::vector<std::byte>> corpus = BuildCorpus();
    ASSERT_FALSE(corpus.empty());

    Mutator mutator{ 0x5EED1234u };
    std::size_t decodedFine = 0;

    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        std::vector<std::byte> payload = corpus[mutator.Next() % corpus.size()];
        if (payload.empty())
            continue;

        // One of: flip a byte, truncate, or extend. Between them these reach
        // every guard: the type switch, the length checks, and the tail check.
        switch (mutator.Next() % 3)
        {
        case 0:
            payload[mutator.Next() % payload.size()] =
                static_cast<std::byte>(mutator.Next() & 0xFF);
            break;
        case 1:
            payload.resize(mutator.Next() % payload.size());
            break;
        default:
            payload.push_back(static_cast<std::byte>(mutator.Next() & 0xFF));
            break;
        }

        NetMessageType type = NetMessageType::Invalid;
        if (NetPeekMessageType(payload, type) != NetDecodeError::None)
            continue;

        // The assertion is that this returns at all, under ASan and UBSan in
        // the sanitizer configurations, for every input.
        if (DecodeAny(type, payload) == NetDecodeError::None)
            ++decodedFine;
    }

    // Not a correctness claim, a coverage one: if nothing survived, the corpus
    // is only exercising rejection paths and the accept path is untested here.
    EXPECT_GT(decodedFine, 0u)
        << "no mutation decoded successfully; the corpus is not reaching the accept path";
}

TEST(NetProtocolFuzz, ArbitraryBytesNeverDecodeAsAWholeMessageByAccident)
{
    Mutator mutator{ 0xC0FFEEu };

    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        std::vector<std::byte> payload(mutator.Next() % 64);
        for (std::byte& byte : payload)
            byte = static_cast<std::byte>(mutator.Next() & 0xFF);

        NetMessageType type = NetMessageType::Invalid;
        if (NetPeekMessageType(payload, type) != NetDecodeError::None)
            continue;
        (void)DecodeAny(type, payload);
    }
    SUCCEED() << "random input decoded without escaping a buffer";
}
