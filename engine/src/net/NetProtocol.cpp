#include <net/NetProtocol.h>

#include <bit>
#include <cstring>

static_assert(std::endian::native == std::endian::little,
              "the wire format is little-endian and the codec does not swap; "
              "a big-endian target needs the swap written and tested, not assumed");

namespace
{
    const NetCaps kDefaultCaps{};

    // Every encoder writes into a caller-owned buffer of at most one datagram,
    // so the scratch size is the transport bound rather than anything per
    // message. Encoding is not on a hot path; decoding is the one that matters.
    template <typename TWrite>
    std::span<const std::byte> Encode(NetMessageType type,
                                      std::span<std::byte> out,
                                      TWrite&& write)
    {
        NetWriter writer(out);
        writer.WriteU8(static_cast<std::uint8_t>(type));
        write(writer);
        if (writer.Overflowed())
            return {};
        return writer.Written();
    }

    // Shared tail for every decoder: the reader's own failure wins, and a
    // message that decoded but left bytes behind is refused.
    NetDecodeError Finish(const NetReader& reader)
    {
        if (reader.Error() != NetDecodeError::None)
            return reader.Error();
        if (!reader.AtEnd())
            return NetDecodeError::TrailingBytes;
        return NetDecodeError::None;
    }

    // Consumes and checks the leading type byte. A decoder called with the
    // wrong message's bytes fails here rather than misreading the body.
    bool TakeType(NetReader& reader, NetMessageType expected)
    {
        std::uint8_t raw = 0;
        if (!reader.ReadU8(raw))
            return false;
        return static_cast<NetMessageType>(raw) == expected;
    }
}

std::string_view NetDecodeErrorToString(NetDecodeError error)
{
    switch (error)
    {
    case NetDecodeError::None:           return "ok";
    case NetDecodeError::Truncated:      return "truncated";
    case NetDecodeError::UnknownMessage: return "unknown message type";
    case NetDecodeError::BadVersion:     return "protocol version mismatch";
    case NetDecodeError::CapExceeded:    return "length past its cap";
    case NetDecodeError::Malformed:      return "malformed field";
    case NetDecodeError::TrailingBytes:  return "trailing bytes";
    }
    return "unknown error";
}

const NetCaps& NetDefaultCaps()
{
    return kDefaultCaps;
}

//=============================================================================
// NetWriter
//=============================================================================

bool NetWriter::Reserve(std::size_t bytes)
{
    if (Overflow)
        return false;
    if (Buffer.size() - Cursor < bytes)
    {
        Overflow = true;
        return false;
    }
    return true;
}

void NetWriter::WriteU8(std::uint8_t value)
{
    if (!Reserve(1))
        return;
    Buffer[Cursor++] = static_cast<std::byte>(value);
}

void NetWriter::WriteU16(std::uint16_t value)
{
    if (!Reserve(sizeof(value)))
        return;
    std::memcpy(Buffer.data() + Cursor, &value, sizeof(value));
    Cursor += sizeof(value);
}

void NetWriter::WriteU32(std::uint32_t value)
{
    if (!Reserve(sizeof(value)))
        return;
    std::memcpy(Buffer.data() + Cursor, &value, sizeof(value));
    Cursor += sizeof(value);
}

void NetWriter::WriteU64(std::uint64_t value)
{
    if (!Reserve(sizeof(value)))
        return;
    std::memcpy(Buffer.data() + Cursor, &value, sizeof(value));
    Cursor += sizeof(value);
}

void NetWriter::WriteBytes(std::span<const std::byte> value, std::size_t cap)
{
    // Refused rather than truncated: silently shortening a field would produce
    // a message the peer decodes successfully and misunderstands.
    if (value.size() > cap || value.size() > 0xFFFF)
    {
        Overflow = true;
        return;
    }
    WriteU16(static_cast<std::uint16_t>(value.size()));
    if (!Reserve(value.size()))
        return;
    if (!value.empty())
        std::memcpy(Buffer.data() + Cursor, value.data(), value.size());
    Cursor += value.size();
}

void NetWriter::WriteString(std::string_view value, std::size_t cap)
{
    WriteBytes(std::span<const std::byte>(
                   reinterpret_cast<const std::byte*>(value.data()), value.size()),
               cap);
}

//=============================================================================
// NetReader
//=============================================================================

bool NetReader::Take(std::size_t bytes, const std::byte*& out)
{
    if (Failure != NetDecodeError::None)
        return false;
    if (Buffer.size() - Cursor < bytes)
    {
        Failure = NetDecodeError::Truncated;
        return false;
    }
    out = Buffer.data() + Cursor;
    Cursor += bytes;
    return true;
}

bool NetReader::ReadU8(std::uint8_t& out)
{
    const std::byte* at = nullptr;
    if (!Take(1, at))
        return false;
    out = static_cast<std::uint8_t>(*at);
    return true;
}

bool NetReader::ReadU16(std::uint16_t& out)
{
    const std::byte* at = nullptr;
    if (!Take(sizeof(out), at))
        return false;
    std::memcpy(&out, at, sizeof(out));
    return true;
}

bool NetReader::ReadU32(std::uint32_t& out)
{
    const std::byte* at = nullptr;
    if (!Take(sizeof(out), at))
        return false;
    std::memcpy(&out, at, sizeof(out));
    return true;
}

bool NetReader::ReadU64(std::uint64_t& out)
{
    const std::byte* at = nullptr;
    if (!Take(sizeof(out), at))
        return false;
    std::memcpy(&out, at, sizeof(out));
    return true;
}

bool NetReader::ReadBytes(std::vector<std::byte>& out, std::size_t cap)
{
    std::uint16_t length = 0;
    if (!ReadU16(length))
        return false;

    // Checked against the cap AND against what is actually left, before the
    // vector is sized. This ordering is the whole point: a wire-supplied length
    // must never reach an allocation.
    if (length > cap)
    {
        Failure = NetDecodeError::CapExceeded;
        return false;
    }
    const std::byte* at = nullptr;
    if (!Take(length, at))
        return false;

    out.assign(at, at + length);
    return true;
}

bool NetReader::ReadString(std::string& out, std::size_t cap)
{
    std::uint16_t length = 0;
    if (!ReadU16(length))
        return false;
    if (length > cap)
    {
        Failure = NetDecodeError::CapExceeded;
        return false;
    }
    const std::byte* at = nullptr;
    if (!Take(length, at))
        return false;

    out.assign(reinterpret_cast<const char*>(at), length);
    return true;
}

//=============================================================================
// Encoders
//=============================================================================

std::span<const std::byte> NetEncodeHello(const NetHello& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Hello, out, [&](NetWriter& w) {
        w.WriteU16(message.ProtocolVersion);
    });
}

std::span<const std::byte> NetEncodeChallenge(const NetChallenge& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Challenge, out, [&](NetWriter& w) {
        w.WriteBytes(message.Cookie, NetDefaultCaps().MaxCookieBytes);
    });
}

std::span<const std::byte> NetEncodeCookieEcho(const NetCookieEcho& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::CookieEcho, out, [&](NetWriter& w) {
        w.WriteBytes(message.Cookie, NetDefaultCaps().MaxCookieBytes);
        w.WriteU16(message.ProtocolVersion);
        w.WriteU64(message.ModuleFingerprint);
        w.WriteU64(message.WorldIdentity);
        w.WriteU32(message.FixedTickRateMilliHz);
    });
}

std::span<const std::byte> NetEncodeAccept(const NetAccept& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Accept, out, [&](NetWriter& w) {
        w.WriteU32(message.PeerId);
        w.WriteU64(message.ModuleFingerprint);
        w.WriteU64(message.WorldIdentity);
        w.WriteU32(message.FixedTickRateMilliHz);
        w.WriteU64(message.AuthorityTick);
    });
}

std::span<const std::byte> NetEncodeRefuse(const NetRefuse& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Refuse, out, [&](NetWriter& w) {
        w.WriteString(message.Reason, NetDefaultCaps().MaxReasonBytes);
    });
}

std::span<const std::byte> NetEncodeDisconnect(const NetDisconnect& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Disconnect, out, [&](NetWriter& w) {
        w.WriteString(message.Reason, NetDefaultCaps().MaxReasonBytes);
    });
}

std::span<const std::byte> NetEncodePing(const NetPing& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Ping, out, [&](NetWriter& w) {
        w.WriteU64(message.SentMicroseconds);
    });
}

std::span<const std::byte> NetEncodePong(const NetPong& message, std::span<std::byte> out)
{
    return Encode(NetMessageType::Pong, out, [&](NetWriter& w) {
        w.WriteU64(message.SentMicroseconds);
        w.WriteU64(message.AuthorityTick);
    });
}

//=============================================================================
// Decoders
//=============================================================================

NetDecodeError NetPeekMessageType(std::span<const std::byte> payload, NetMessageType& out)
{
    if (payload.empty())
        return NetDecodeError::Truncated;

    switch (static_cast<NetMessageType>(payload[0]))
    {
    case NetMessageType::Hello:
    case NetMessageType::Challenge:
    case NetMessageType::CookieEcho:
    case NetMessageType::Accept:
    case NetMessageType::Refuse:
    case NetMessageType::Disconnect:
    case NetMessageType::Ping:
    case NetMessageType::Pong:
        out = static_cast<NetMessageType>(payload[0]);
        return NetDecodeError::None;
    case NetMessageType::Invalid:
        break;
    }
    // Listed exhaustively rather than with a default arm so a new message type
    // that nobody routes is a compile warning here rather than a silent accept.
    out = NetMessageType::Invalid;
    return NetDecodeError::UnknownMessage;
}

NetDecodeError NetDecodeHello(std::span<const std::byte> payload, NetHello& out)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Hello))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadU16(out.ProtocolVersion))
        return reader.Error();
    // Refused here rather than later: a version this build cannot speak must
    // not reach any code that assumes it can.
    if (out.ProtocolVersion != kNetProtocolVersion)
        return NetDecodeError::BadVersion;
    return Finish(reader);
}

NetDecodeError NetDecodeChallenge(std::span<const std::byte> payload,
                                  NetChallenge& out,
                                  const NetCaps& caps)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Challenge))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadBytes(out.Cookie, caps.MaxCookieBytes))
        return reader.Error();
    if (out.Cookie.empty())
        return NetDecodeError::Malformed;
    return Finish(reader);
}

NetDecodeError NetDecodeCookieEcho(std::span<const std::byte> payload,
                                   NetCookieEcho& out,
                                   const NetCaps& caps)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::CookieEcho))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadBytes(out.Cookie, caps.MaxCookieBytes))
        return reader.Error();
    if (out.Cookie.empty())
        return NetDecodeError::Malformed;
    if (!reader.ReadU16(out.ProtocolVersion))
        return reader.Error();
    if (out.ProtocolVersion != kNetProtocolVersion)
        return NetDecodeError::BadVersion;
    if (!reader.ReadU64(out.ModuleFingerprint)
        || !reader.ReadU64(out.WorldIdentity)
        || !reader.ReadU32(out.FixedTickRateMilliHz))
    {
        return reader.Error();
    }
    // A zero tick rate would divide by zero wherever the schedule consumes it.
    if (out.FixedTickRateMilliHz == 0)
        return NetDecodeError::Malformed;
    return Finish(reader);
}

NetDecodeError NetDecodeAccept(std::span<const std::byte> payload, NetAccept& out)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Accept))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadU32(out.PeerId)
        || !reader.ReadU64(out.ModuleFingerprint)
        || !reader.ReadU64(out.WorldIdentity)
        || !reader.ReadU32(out.FixedTickRateMilliHz)
        || !reader.ReadU64(out.AuthorityTick))
    {
        return reader.Error();
    }
    // Zero is the reserved invalid peer, so an authority claiming it is either
    // broken or trying to make a client's peer table collide with "no peer".
    if (out.PeerId == 0)
        return NetDecodeError::Malformed;
    if (out.FixedTickRateMilliHz == 0)
        return NetDecodeError::Malformed;
    return Finish(reader);
}

NetDecodeError NetDecodeRefuse(std::span<const std::byte> payload,
                               NetRefuse& out,
                               const NetCaps& caps)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Refuse))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadString(out.Reason, caps.MaxReasonBytes))
        return reader.Error();
    return Finish(reader);
}

NetDecodeError NetDecodeDisconnect(std::span<const std::byte> payload,
                                   NetDisconnect& out,
                                   const NetCaps& caps)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Disconnect))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadString(out.Reason, caps.MaxReasonBytes))
        return reader.Error();
    return Finish(reader);
}

NetDecodeError NetDecodePing(std::span<const std::byte> payload, NetPing& out)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Ping))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadU64(out.SentMicroseconds))
        return reader.Error();
    return Finish(reader);
}

NetDecodeError NetDecodePong(std::span<const std::byte> payload, NetPong& out)
{
    NetReader reader(payload);
    if (!TakeType(reader, NetMessageType::Pong))
        return NetDecodeError::UnknownMessage;
    if (!reader.ReadU64(out.SentMicroseconds) || !reader.ReadU64(out.AuthorityTick))
        return reader.Error();
    return Finish(reader);
}
