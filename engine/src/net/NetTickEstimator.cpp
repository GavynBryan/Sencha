#include <net/NetTickEstimator.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Round trips are measured, so they carry noise; rounding up is the safe
    // direction. A flight time guessed short stamps commands that arrive after
    // the tick they were meant for, which is input lost -- guessed long, they
    // wait one extra tick, which is input late. Late beats lost.
    [[nodiscard]] std::uint32_t FlightTicksFrom(std::uint64_t roundTripMicroseconds,
                                                double tickSeconds)
    {
        if (tickSeconds <= 0.0)
            return 0;

        const double oneWaySeconds =
            static_cast<double>(roundTripMicroseconds) * 0.5e-6;
        const double ticks = std::ceil(oneWaySeconds / tickSeconds);
        if (!(ticks > 0.0))
            return 0;

        // A round trip long enough to overflow this is a connection that has
        // already failed every other way.
        return static_cast<std::uint32_t>(
            std::min(ticks, static_cast<double>(1u << 16)));
    }

    [[nodiscard]] std::uint64_t AddOffset(std::uint64_t tick, std::int64_t offset)
    {
        if (offset >= 0)
            return tick + static_cast<std::uint64_t>(offset);

        const auto back = static_cast<std::uint64_t>(-offset);
        // Tick zero is a real tick and there is nothing before it, so a
        // negative estimate early in a session floors rather than wrapping.
        return back > tick ? 0 : tick - back;
    }
}

void NetTickEstimator::Observe(std::uint64_t authorityTick, std::uint64_t localTick,
                               std::uint64_t roundTripMicroseconds,
                               double tickSeconds)
{
    if (tickSeconds <= 0.0)
        return;

    Flight = FlightTicksFrom(roundTripMicroseconds, tickSeconds);

    // The sample is a round trip old, so the authority has moved on since it
    // spoke: what it is at now is what it said plus the flight time.
    const std::int64_t observed =
        static_cast<std::int64_t>(authorityTick) + static_cast<std::int64_t>(Flight)
        - static_cast<std::int64_t>(localTick);

    if (!Observed)
    {
        Delta = observed;
        Observed = true;
        return;
    }

    const std::int64_t difference = observed - Delta;
    if (difference > kSnapTicks || difference < -kSnapTicks)
    {
        Delta = observed;
        return;
    }

    Delta += std::clamp(difference, -kSlewTicks, kSlewTicks);
}

std::uint64_t NetTickEstimator::AuthorityTickAt(std::uint64_t localTick) const
{
    return AddOffset(localTick, Delta);
}

std::int64_t NetTickEstimator::CommandOffset() const
{
    return Delta + static_cast<std::int64_t>(Flight)
           + static_cast<std::int64_t>(Slack);
}

std::uint64_t NetTickEstimator::CommandTickAt(std::uint64_t localTick) const
{
    return AddOffset(localTick, CommandOffset());
}

void NetTickEstimator::Reset()
{
    *this = NetTickEstimator{};
}
