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
    Delaying.clear();
    Inner.Close();
}

bool SimulatedTransport::Dispatch(const NetAddress& to,
                                  std::span<const std::byte> payload,
                                  std::uint32_t jitterRoll)
{
    const std::uint32_t jitter = Impairment.JitterSteps == 0
        ? 0
        : jitterRoll % (Impairment.JitterSteps + 1);
    const std::uint32_t delay = Impairment.LatencySteps + jitter;
    if (delay == 0)
        return Inner.Send(to, payload);

    Postponed postponed;
    postponed.To = to;
    postponed.Payload.assign(payload.begin(), payload.end());
    postponed.ReleaseStep = StepIndex + delay;
    Delaying.push_back(std::move(postponed));
    ++DelayedCount;
    return true;
}

void SimulatedTransport::ReleaseDue()
{
    if (Delaying.empty())
        return;

    // Partitioned rather than sorted: two datagrams given different delays are
    // meant to come out in whichever order their release steps put them, and
    // that is the reordering jitter causes.
    std::size_t kept = 0;
    for (std::size_t index = 0; index < Delaying.size(); ++index)
    {
        if (Delaying[index].ReleaseStep <= StepIndex)
        {
            (void)Inner.Send(Delaying[index].To, Delaying[index].Payload);
            continue;
        }
        if (kept != index)
            Delaying[kept] = std::move(Delaying[index]);
        ++kept;
    }
    Delaying.resize(kept);
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
    const std::uint32_t jitterRoll = NextRoll();

    if (lossRoll < Impairment.LossPercent)
    {
        ++DroppedCount;
        // Reported as sent: the sender cannot tell, and a transport that
        // admitted the drop would be a transport nobody has to be robust to.
        return true;
    }

    // Anything held from an earlier send goes out first now, behind whatever
    // this call is about to send -- that is the reorder. It carries the jitter
    // roll it was given when it arrived, so a held datagram's delay is decided
    // by the same fixed number of rolls per send as any other.
    const auto releaseHeld = [this]() {
        for (Held& held : Holding)
            (void)Dispatch(held.To, held.Payload, held.JitterRoll);
        Holding.clear();
    };

    if (reorderRoll < Impairment.ReorderPercent)
    {
        Held held;
        held.To = to;
        held.Payload.assign(payload.begin(), payload.end());
        held.JitterRoll = jitterRoll;
        Holding.push_back(std::move(held));
        ++ReorderedCount;
        return true;
    }

    const bool sent = Dispatch(to, payload, jitterRoll);
    if (duplicateRoll < Impairment.DuplicatePercent)
    {
        ++DuplicatedCount;
        (void)Dispatch(to, payload, jitterRoll);
    }
    releaseHeld();
    return sent;
}

void SimulatedTransport::Flush()
{
    for (Held& held : Holding)
        (void)Inner.Send(held.To, held.Payload);
    Holding.clear();

    for (Postponed& postponed : Delaying)
        (void)Inner.Send(postponed.To, postponed.Payload);
    Delaying.clear();
}

std::span<const NetDatagram> SimulatedTransport::Receive()
{
    // Receive is the pump's edge -- every endpoint calls it once a frame,
    // including one with nothing to say -- so it is what advances the delay
    // line. Tying the step to Send instead would strand a datagram whenever the
    // sender fell quiet, which is exactly when a delayed one still has to land.
    ++StepIndex;
    ReleaseDue();
    return Inner.Receive();
}
