#pragma once

#include <net/NetAddress.h>
#include <net/NetTransport.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// NetProtocol
//
// Encode and decode, as pure functions over spans. No sockets, no engine
// services, no logging, no allocation sized by anything the wire said. This is
// the one boundary every byte from a peer crosses, so it is written to be read
// by a reviewer and run by a fuzzer, and it is deliberately separable from
// everything that has state.
//
// The boundary is symmetric. A client decodes an authority's bytes under the
// same rules an authority decodes a client's, because a compromised server
// attacking a client is in the threat model and the client-facing message set
// is the larger one.
//
// Rules every decoder here follows:
//   - Length-prefixed everything; every count checked against the bytes that
//     remain and against a cap before it is used to size or index anything.
//   - Failure is a typed error, never a partial value and never a throw.
//   - Unknown message types are an error, not a skip: a peer sending one is
//     either a version the gate should have refused or something probing.
//=============================================================================

// Bumped on any wire change. There are no cross-version sessions: the handshake
// refuses a mismatch with a reason rather than trying to negotiate, which is
// the only honest posture while the format is still moving.
inline constexpr std::uint16_t kNetProtocolVersion = 1;

// Every message begins with this so a decoder can dispatch before it trusts
// anything else. Values are explicit because they are wire format: reordering
// the enum must not silently change what a peer thinks it received.
enum class NetMessageType : std::uint8_t
{
    Invalid = 0,
    // Handshake. The cookie exchange is stateless on the authority: no session
    // state and no allocation exist for a peer until it echoes a cookie bound
    // to its own address.
    Hello = 1,
    Challenge = 2,
    CookieEcho = 3,
    Accept = 4,
    Refuse = 5,
    Disconnect = 6,
    // Liveness and clock sync.
    Ping = 7,
    Pong = 8,
};

// What a decode can go wrong as. A peer's strike count keys on these, so they
// distinguish "malformed" from "not for me" -- the first is hostile, the second
// is ordinary crosstalk on a shared port.
enum class NetDecodeError : std::uint8_t
{
    None = 0,
    Truncated,        // ran out of bytes mid-field
    UnknownMessage,   // type byte names nothing this build has
    BadVersion,       // protocol version mismatch in a header
    CapExceeded,      // a length or count past its cap table entry
    Malformed,        // a field that decoded but cannot be valid
    TrailingBytes,    // decoded fine and then had bytes left over
};

[[nodiscard]] std::string_view NetDecodeErrorToString(NetDecodeError error);

//=============================================================================
// Cap table
//
// Every bound the decoders enforce, in one place, so a reviewer can see the
// whole allocation surface a hostile peer can reach without reading each
// decoder. Nothing here is derived from the wire.
//=============================================================================
struct NetCaps
{
    // Refusal and disconnect carry text meant for a human. It is capped, and it
    // is rendered as text and never interpreted.
    std::size_t MaxReasonBytes = 128;
    // The world and build identity strings the compatibility gate compares.
    std::size_t MaxIdentityBytes = 96;
    std::size_t MaxCookieBytes = 32;
};

[[nodiscard]] const NetCaps& NetDefaultCaps();

//=============================================================================
// Messages
//
// Value types. A decoder returns one fully formed or returns an error; there is
// no half-decoded state a caller could act on by mistake.
//=============================================================================

// The client's opening word. Carries nothing the authority has to remember,
// which is what makes the cookie exchange stateless.
struct NetHello
{
    std::uint16_t ProtocolVersion = kNetProtocolVersion;
};

// The authority's stateless reply: an opaque cookie the client must echo. It is
// an HMAC of the client's address and a rotating secret, so it verifies without
// the authority storing anything per unverified peer. A spoofed source address
// never receives the cookie it would have to echo.
struct NetChallenge
{
    std::vector<std::byte> Cookie;
};

// The verified hello. Everything the compatibility gate compares travels here,
// so a mismatch is refused before any session state exists.
struct NetCookieEcho
{
    std::vector<std::byte> Cookie;
    std::uint16_t ProtocolVersion = kNetProtocolVersion;
    // The game module ABI fingerprint, already computed at build time. Two
    // builds whose components disagree cannot share a wire format.
    std::uint64_t ModuleFingerprint = 0;
    // A hash over the cooked content both sides must already own. The wire
    // never carries content; this is what makes that safe to assume.
    std::uint64_t WorldIdentity = 0;
    // Handshake-verified rather than synced: a tick rate cannot change live, so
    // a mismatch refuses admission instead of growing sync machinery.
    std::uint32_t FixedTickRateMilliHz = 0;
};

struct NetAccept
{
    std::uint32_t PeerId = 0;
    // The authority's identity tuple, so the gate runs in both directions and a
    // client refuses a mismatched authority before applying any state.
    std::uint64_t ModuleFingerprint = 0;
    std::uint64_t WorldIdentity = 0;
    std::uint32_t FixedTickRateMilliHz = 0;
    std::uint64_t AuthorityTick = 0;
};

struct NetRefuse
{
    std::string Reason;
};

struct NetDisconnect
{
    std::string Reason;
};

// Clock sync and liveness. The timestamp is echoed verbatim so the sender can
// measure a round trip without keeping a table of outstanding pings.
struct NetPing
{
    std::uint64_t SentMicroseconds = 0;
};

struct NetPong
{
    std::uint64_t SentMicroseconds = 0;
    std::uint64_t AuthorityTick = 0;
};

//=============================================================================
// Writer and reader
//
// Little-endian, asserted rather than swapped: every current and plausible
// target is little-endian, and a swap path nothing exercises is a bug waiting
// for the first big-endian port rather than portability.
//=============================================================================
class NetWriter
{
public:
    explicit NetWriter(std::span<std::byte> buffer) : Buffer(buffer) {}

    void WriteU8(std::uint8_t value);
    void WriteU16(std::uint16_t value);
    void WriteU32(std::uint32_t value);
    void WriteU64(std::uint64_t value);
    // Length-prefixed; refuses (and marks overflow) past the cap.
    void WriteBytes(std::span<const std::byte> value, std::size_t cap);
    void WriteString(std::string_view value, std::size_t cap);

    [[nodiscard]] bool Overflowed() const { return Overflow; }
    [[nodiscard]] std::size_t Size() const { return Cursor; }
    [[nodiscard]] std::span<const std::byte> Written() const
    {
        return Buffer.subspan(0, Cursor);
    }

private:
    [[nodiscard]] bool Reserve(std::size_t bytes);

    std::span<std::byte> Buffer;
    std::size_t Cursor = 0;
    bool Overflow = false;
};

class NetReader
{
public:
    explicit NetReader(std::span<const std::byte> buffer) : Buffer(buffer) {}

    [[nodiscard]] bool ReadU8(std::uint8_t& out);
    [[nodiscard]] bool ReadU16(std::uint16_t& out);
    [[nodiscard]] bool ReadU32(std::uint32_t& out);
    [[nodiscard]] bool ReadU64(std::uint64_t& out);
    // Both check the decoded length against `cap` and against the bytes that
    // actually remain, before reserving anything.
    [[nodiscard]] bool ReadBytes(std::vector<std::byte>& out, std::size_t cap);
    [[nodiscard]] bool ReadString(std::string& out, std::size_t cap);

    [[nodiscard]] std::size_t Remaining() const { return Buffer.size() - Cursor; }
    [[nodiscard]] bool AtEnd() const { return Cursor == Buffer.size(); }
    [[nodiscard]] NetDecodeError Error() const { return Failure; }

private:
    [[nodiscard]] bool Take(std::size_t bytes, const std::byte*& out);

    std::span<const std::byte> Buffer;
    std::size_t Cursor = 0;
    NetDecodeError Failure = NetDecodeError::None;
};

//=============================================================================
// Encode and decode
//
// Encoders return the bytes written, or an empty span if the message could not
// fit -- which is a programming error at these sizes, not a wire condition.
//=============================================================================
[[nodiscard]] std::span<const std::byte> NetEncodeHello(const NetHello&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodeChallenge(const NetChallenge&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodeCookieEcho(const NetCookieEcho&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodeAccept(const NetAccept&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodeRefuse(const NetRefuse&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodeDisconnect(const NetDisconnect&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodePing(const NetPing&, std::span<std::byte> out);
[[nodiscard]] std::span<const std::byte> NetEncodePong(const NetPong&, std::span<std::byte> out);

// Reads the type byte without consuming the message, so a dispatcher can route
// before it decodes. UnknownMessage for a type this build does not define.
[[nodiscard]] NetDecodeError NetPeekMessageType(std::span<const std::byte> payload,
                                                NetMessageType& out);

// One decoder per message. Each returns None only when the whole payload was
// consumed: trailing bytes are an error, because a decoder that ignores them
// lets a peer smuggle content past a reviewer's reading of the format.
[[nodiscard]] NetDecodeError NetDecodeHello(std::span<const std::byte>, NetHello&);
[[nodiscard]] NetDecodeError NetDecodeChallenge(std::span<const std::byte>, NetChallenge&, const NetCaps& = NetDefaultCaps());
[[nodiscard]] NetDecodeError NetDecodeCookieEcho(std::span<const std::byte>, NetCookieEcho&, const NetCaps& = NetDefaultCaps());
[[nodiscard]] NetDecodeError NetDecodeAccept(std::span<const std::byte>, NetAccept&);
[[nodiscard]] NetDecodeError NetDecodeRefuse(std::span<const std::byte>, NetRefuse&, const NetCaps& = NetDefaultCaps());
[[nodiscard]] NetDecodeError NetDecodeDisconnect(std::span<const std::byte>, NetDisconnect&, const NetCaps& = NetDefaultCaps());
[[nodiscard]] NetDecodeError NetDecodePing(std::span<const std::byte>, NetPing&);
[[nodiscard]] NetDecodeError NetDecodePong(std::span<const std::byte>, NetPong&);
