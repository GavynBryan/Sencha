#pragma once

#include <net/NetTransport.h>

#include <cstdint>
#include <vector>

//=============================================================================
// SimulatedTransport
//
// Wraps another transport and applies a seeded impairment schedule to what
// passes through it: loss, duplication, and reordering. Same seed, same
// schedule, same outcome, every run and every machine — a reliability defect
// found here is reproducible from the seed alone rather than from having been
// unlucky on somebody's wifi.
//
// It is the reason the reliability layer can be tested at all. Delivery over a
// perfect in-process pair proves the happy path and nothing else; the resend,
// ack, and dedup paths only execute when something goes wrong, and this is what
// makes something go wrong on purpose.
//
// Dev-only by construction: nothing in a shipping session constructs one.
//=============================================================================
struct NetImpairment
{
    // Parts per hundred, applied per datagram at send.
    std::uint32_t LossPercent = 0;
    std::uint32_t DuplicatePercent = 0;
    // A held datagram is released behind the one sent after it, which is what
    // reordering looks like from the receiver: later sequence first.
    std::uint32_t ReorderPercent = 0;
    std::uint64_t Seed = 1;
};

class SimulatedTransport final : public INetTransport
{
public:
    SimulatedTransport(INetTransport& inner, NetImpairment impairment);

    [[nodiscard]] bool Open(std::uint16_t port) override;
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] NetAddress LocalAddress() const override;
    [[nodiscard]] bool Send(const NetAddress& to,
                            std::span<const std::byte> payload) override;
    [[nodiscard]] std::span<const NetDatagram> Receive() override;

    // What the schedule actually did, so a test asserts against the impairment
    // it got rather than the one it asked for.
    [[nodiscard]] std::uint64_t Dropped() const { return DroppedCount; }
    [[nodiscard]] std::uint64_t Duplicated() const { return DuplicatedCount; }
    [[nodiscard]] std::uint64_t Reordered() const { return ReorderedCount; }

    // Releases anything held back for reordering. A test that stops sending
    // still wants the held datagram delivered rather than lost.
    void Flush();

private:
    struct Held
    {
        NetAddress To;
        std::vector<std::byte> Payload;
    };

    [[nodiscard]] std::uint32_t NextRoll();

    INetTransport& Inner;
    NetImpairment Impairment;
    std::uint64_t RandomState = 0;
    std::vector<Held> Holding;
    std::uint64_t DroppedCount = 0;
    std::uint64_t DuplicatedCount = 0;
    std::uint64_t ReorderedCount = 0;
};
