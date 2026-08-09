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

    // Delay before a datagram goes out, counted in pump steps: one step is one
    // Receive on the sending endpoint, which is one frame. The transport owns no
    // clock -- a caller converts from its own tick rate, and gets the same
    // schedule at any wall-clock speed as a result.
    //
    // Latency is the impairment prediction exists for. Loss and reorder exercise
    // the reliability layer; only delay makes an authority's word old enough
    // that a client has to guess ahead of it.
    std::uint32_t LatencySteps = 0;
    // Drawn per datagram on top of the latency. This is also what reorders a
    // stream no reorder roll touched -- two datagrams given different delays
    // arrive in the other order, which is where real jitter's reordering comes
    // from.
    std::uint32_t JitterSteps = 0;

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
    [[nodiscard]] std::uint64_t Delayed() const { return DelayedCount; }

    // Releases anything held back for reordering or delay. A test that stops
    // sending still wants the held datagram delivered rather than lost.
    void Flush();

private:
    struct Held
    {
        NetAddress To;
        std::vector<std::byte> Payload;
        // Drawn when the datagram was held, so releasing it costs no extra roll
        // and the schedule stays a fixed number of rolls per send.
        std::uint32_t JitterRoll = 0;
    };

    struct Postponed
    {
        NetAddress To;
        std::vector<std::byte> Payload;
        std::uint64_t ReleaseStep = 0;
    };

    [[nodiscard]] std::uint32_t NextRoll();
    // Sends now, or queues for a later step when the schedule says this datagram
    // is delayed. Reports what the inner transport said when it went out now,
    // and true when it was queued -- a sender cannot be told about a failure
    // that has not happened yet.
    [[nodiscard]] bool Dispatch(const NetAddress& to,
                                std::span<const std::byte> payload,
                                std::uint32_t jitterRoll);
    void ReleaseDue();

    INetTransport& Inner;
    NetImpairment Impairment;
    std::uint64_t RandomState = 0;
    std::vector<Held> Holding;
    std::vector<Postponed> Delaying;
    // Advanced once per Receive, which is once per pump on this endpoint.
    std::uint64_t StepIndex = 0;
    std::uint64_t DroppedCount = 0;
    std::uint64_t DuplicatedCount = 0;
    std::uint64_t ReorderedCount = 0;
    std::uint64_t DelayedCount = 0;
};
