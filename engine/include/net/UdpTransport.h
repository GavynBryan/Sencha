#pragma once

#include <net/NetTransport.h>

#include <array>
#include <vector>

//=============================================================================
// UdpTransport
//
// A nonblocking UDP socket. This is the only file in the engine that owns a
// platform socket; the descriptor is held as an int behind this header so no
// socket header reaches any other translation unit (the net isolation fitness
// check enforces it, the way the physics one enforces Jolt).
//
// Pumped from the frame on the owner thread, never from a thread of its own.
// At the tick rates this carries, one frame of added latency is well inside the
// jitter buffer, and a socket thread would buy nothing but a synchronization
// problem. The recorded trigger for revisiting is profiling that shows the pump
// exceeding its frame budget in a real session.
//=============================================================================
class UdpTransport final : public INetTransport
{
public:
    UdpTransport() = default;
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    [[nodiscard]] bool Open(std::uint16_t port) override;
    void Close() override;
    [[nodiscard]] bool IsOpen() const override { return Socket >= 0; }
    [[nodiscard]] NetAddress LocalAddress() const override { return Local; }
    [[nodiscard]] bool Send(const NetAddress& to,
                            std::span<const std::byte> payload) override;
    [[nodiscard]] std::span<const NetDatagram> Receive() override;

    // Bounds one pump so a flood cannot hold the frame open indefinitely; the
    // rest waits in the socket buffer for the next frame or is dropped by the
    // kernel, which is the backpressure a budget is supposed to produce.
    void SetMaxDatagramsPerReceive(std::size_t count) { MaxPerReceive = count; }

    // Datagrams the last Receive refused because they were larger than the
    // transport allows. A peer sending them is either broken or probing, and
    // the session layer counts that against it.
    [[nodiscard]] std::uint64_t OversizedDropped() const { return Oversized; }

private:
    struct Pending
    {
        NetAddress From;
        std::size_t Slot = 0;
        std::size_t Length = 0;
    };

    int Socket = -1;
    NetAddress Local;
    std::size_t MaxPerReceive = 256;
    std::uint64_t Oversized = 0;

    // One slab of fixed-size slots rather than a vector per datagram: the pump
    // runs every frame and must not allocate in steady state.
    std::vector<std::array<std::byte, kNetMaxDatagramBytes>> Slots;
    std::vector<Pending> Pendings;
    std::vector<NetDatagram> Received;
};
