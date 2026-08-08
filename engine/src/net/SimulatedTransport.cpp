#include <net/SimulatedTransport.h>

#include <utility>

SimulatedTransport::SimulatedTransport(INetTransport& inner, NetImpairment impairment)
    : Inner(inner)
    , Impairment(impairment)
    // A zero seed would leave the generator stuck, and a caller who left the
    // seed at its default still wants a usable schedule.
    , RandomState(impairment.Seed != 0 ? impairment.Seed : 1)
{
}

// PCG-style xorshift-multiply. Small, seeded, and identical everywhere, which
// is the entire requirement: the schedule has to replay, not to be strong.
std::uint32_t SimulatedTransport::NextRoll()
{
    RandomState ^= RandomState << 13;
    RandomState ^= RandomState >> 7;
    RandomState ^= RandomState << 17;
    return static_cast<std::uint32_t>((RandomState >> 33) % 100u);
}

bool SimulatedTransport::Open(std::uint16_t port)
{
    return Inner.Open(port);
}

void SimulatedTransport::Close()
{
    Holding.clear();
    Inner.Close();
}

bool SimulatedTransport::IsOpen() const
{
    return Inner.IsOpen();
}

NetAddress SimulatedTransport::LocalAddress() const
{
    return Inner.LocalAddress();
}

bool SimulatedTransport::Send(const NetAddress& to, std::span<const std::byte> payload)
{
    if (!IsOpen())
        return false;
    if (payload.size() > kNetMaxDatagramBytes)
        return false;

    // Rolled before the drop test so the sequence of rolls does not depend on
    // which branch was taken; a schedule that shifts under its own outcomes is
    // not reproducible across a change in the code under test.
    const std::uint32_t lossRoll = NextRoll();
    const std::uint32_t duplicateRoll = NextRoll();
    const std::uint32_t reorderRoll = NextRoll();

    if (lossRoll < Impairment.LossPercent)
    {
        ++DroppedCount;
        // Reported as sent: the sender cannot tell, and a transport that
        // admitted the drop would be a transport nobody has to be robust to.
        return true;
    }

    // Anything held from an earlier send goes out first now, behind whatever
    // this call is about to send -- that is the reorder.
    const auto releaseHeld = [this]() {
        for (Held& held : Holding)
            (void)Inner.Send(held.To, held.Payload);
        Holding.clear();
    };

    if (reorderRoll < Impairment.ReorderPercent)
    {
        Held held;
        held.To = to;
        held.Payload.assign(payload.begin(), payload.end());
        Holding.push_back(std::move(held));
        ++ReorderedCount;
        return true;
    }

    const bool sent = Inner.Send(to, payload);
    if (duplicateRoll < Impairment.DuplicatePercent)
    {
        ++DuplicatedCount;
        (void)Inner.Send(to, payload);
    }
    releaseHeld();
    return sent;
}

void SimulatedTransport::Flush()
{
    for (Held& held : Holding)
        (void)Inner.Send(held.To, held.Payload);
    Holding.clear();
}

std::span<const NetDatagram> SimulatedTransport::Receive()
{
    return Inner.Receive();
}
