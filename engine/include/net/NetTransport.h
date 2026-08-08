#pragma once

#include <net/NetAddress.h>

#include <cstddef>
#include <span>

//=============================================================================
// INetTransport
//
// Unreliable datagrams to and from an address, and nothing else. No ordering,
// no delivery guarantee, no framing: those are the channel layer's job, above
// this. Keeping the seam this narrow is what lets the same channels and the
// same protocol run over a real socket, over an in-process pair, and over a
// deliberately hostile simulation without knowing which they are on.
//
// The seam is earned three times over: a platform boundary (POSIX sockets now,
// winsock and a platform relay later), a test boundary (an in-process pair with
// no socket, so protocol tests are deterministic and need no ports), and a real
// second implementation on day one (the simulated transport).
//
// Every implementation is owner-thread only. Sockets are pumped from the frame,
// not from a thread of their own.
//=============================================================================

// Datagrams larger than this are refused at send rather than fragmented. UDP
// fragmentation is a reliability cliff: one lost fragment loses the whole
// datagram, and path MTU below the usual 1500 is common enough that a
// conservative bound costs less than the tail it avoids. Payloads that do not
// fit are the reliable channel's problem to split, above this layer.
inline constexpr std::size_t kNetMaxDatagramBytes = 1200;

struct NetDatagram
{
    NetAddress From;
    // Borrowed from the transport's own storage. Valid until the next Receive
    // on that transport, which is why the pump copies anything it keeps.
    std::span<const std::byte> Payload;
};

class INetTransport
{
public:
    virtual ~INetTransport() = default;

    // Binds to a local port; zero takes an ephemeral one, which is what a
    // client wants. False if the port is taken or the socket cannot be made.
    [[nodiscard]] virtual bool Open(std::uint16_t port) = 0;
    virtual void Close() = 0;
    [[nodiscard]] virtual bool IsOpen() const = 0;

    // Where this transport receives. Meaningful only while open; the port is
    // resolved after binding, so an ephemeral bind reports its real port here.
    [[nodiscard]] virtual NetAddress LocalAddress() const = 0;

    // False on refusal (not open, oversized, or the platform rejected it).
    // A datagram that leaves the machine and is never delivered still returns
    // true: this layer promises transmission, never delivery.
    [[nodiscard]] virtual bool Send(const NetAddress& to,
                                    std::span<const std::byte> payload) = 0;

    // Drains everything currently readable and returns a view of it. Never
    // blocks. The returned span and every payload in it are invalidated by the
    // next call on this transport.
    [[nodiscard]] virtual std::span<const NetDatagram> Receive() = 0;
};
