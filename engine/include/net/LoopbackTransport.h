#pragma once

#include <net/NetTransport.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class LoopbackTransport;

//=============================================================================
// LoopbackNetwork
//
// The routing table an in-process set of transports share. Delivery is a copy
// into the destination's inbox, so a pair of peers can be stepped by hand: send,
// receive, assert, with no socket, no port, and no scheduling in between. That
// determinism is what makes it the transport the protocol and session tests run
// on, and what makes a failure reproducible from its seed alone.
//
// Addresses are synthesized as loopback with an assigned port. A datagram to an
// address nothing has bound is dropped, exactly as it would be on the wire.
//=============================================================================
class LoopbackNetwork
{
public:
    // Binds a transport at `port`, or assigns one when `port` is zero. Returns
    // an invalid address if the port is already taken.
    [[nodiscard]] NetAddress Bind(LoopbackTransport& transport, std::uint16_t port);
    void Unbind(const NetAddress& address);

    // True when the datagram was queued for a bound peer. False means nothing
    // is listening there, which is a silent drop on a real network too.
    [[nodiscard]] bool Deliver(const NetAddress& to,
                               const NetAddress& from,
                               std::span<const std::byte> payload);

    [[nodiscard]] std::size_t BoundCount() const { return Bound.size(); }

private:
    std::unordered_map<NetAddress, LoopbackTransport*> Bound;
    std::uint16_t NextEphemeralPort = 49152;
};

//=============================================================================
// LoopbackTransport
//
// One endpoint on a LoopbackNetwork. Holds no socket and no thread; a datagram
// is in the peer's inbox the instant Send returns.
//=============================================================================
class LoopbackTransport final : public INetTransport
{
public:
    explicit LoopbackTransport(LoopbackNetwork& network);
    ~LoopbackTransport() override;

    LoopbackTransport(const LoopbackTransport&) = delete;
    LoopbackTransport& operator=(const LoopbackTransport&) = delete;

    [[nodiscard]] bool Open(std::uint16_t port) override;
    void Close() override;
    [[nodiscard]] bool IsOpen() const override { return Local.IsValid(); }
    [[nodiscard]] NetAddress LocalAddress() const override { return Local; }
    [[nodiscard]] bool Send(const NetAddress& to,
                            std::span<const std::byte> payload) override;
    [[nodiscard]] std::span<const NetDatagram> Receive() override;

    // Called by the network when a peer sends here.
    void Accept(const NetAddress& from, std::span<const std::byte> payload);

private:
    struct Pending
    {
        NetAddress From;
        std::vector<std::byte> Payload;
    };

    LoopbackNetwork& Network;
    NetAddress Local;
    // Filled by Accept, moved into Received by the next Receive. Two buffers so
    // a payload handed out stays valid until the caller comes back for more.
    std::vector<Pending> Inbox;
    std::vector<Pending> Delivered;
    std::vector<NetDatagram> Received;
};
